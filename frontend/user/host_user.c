/*
The MIT License (MIT)

Copyright (c) 2014-2015 CSAIL, MIT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "bdbm_drv.h"
#include "debug.h"
#include "platform.h"
#include "host_user.h"
#include "params.h"

#include "utils/utime.h"
#include "utils/uthread.h"


bdbm_host_inf_t _host_user_inf = {
	.ptr_private = NULL,
	.open = host_user_open,
	.close = host_user_close,
	.make_req = host_user_make_req,
	.end_req = host_user_end_req,
};

typedef struct {
	uint64_t nr_host_reqs;
	bdbm_spinlock_t lock;
	bdbm_mutex_t host_lock;
} bdbm_host_block_private_t;


static bdbm_hlm_req_t* __host_block_create_hlm_req (
	bdbm_drv_info_t* bdi, 
	bdbm_host_req_t* host_req)
{
	uint32_t kpg_loop = 0;
	bdbm_hlm_req_t* hlm_req = NULL;

	if ((hlm_req = (bdbm_hlm_req_t*)bdbm_malloc_atomic
			(sizeof (bdbm_hlm_req_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return NULL;
	}

	hlm_req->req_type = host_req->req_type;
	hlm_req->lpa = host_req->lpa;
	hlm_req->len = host_req->len;
	hlm_req->nr_done_reqs = 0;
	hlm_req->ptr_host_req = (void*)host_req;
	hlm_req->ret = 0;
	bdbm_spin_lock_init (&hlm_req->lock);

	if ((hlm_req->pptr_kpgs = (uint8_t**)bdbm_malloc_atomic
			(sizeof(uint8_t*) * hlm_req->len)) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed"); 
		goto fail_req;
	}
	if ((hlm_req->kpg_flags = (uint8_t*)bdbm_malloc_atomic
			(sizeof(uint8_t) * hlm_req->len)) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail_flags;
	}

	/* get or alloc pages */
	for (kpg_loop = 0; kpg_loop < hlm_req->len; kpg_loop++) {
		hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)host_req->data + (kpg_loop * 4096);
		hlm_req->kpg_flags[kpg_loop] = MEMFLAG_KMAP_PAGE;
	}

	if (hlm_req) {
		bdbm_stopwatch_start (&hlm_req->sw);
	}

	return hlm_req;

fail_flags:
	bdbm_free_atomic (hlm_req->pptr_kpgs);

fail_req:
	bdbm_free_atomic (hlm_req);

	return NULL;
}

static void __host_block_delete_hlm_req (
	bdbm_drv_info_t* bdi, 
	bdbm_hlm_req_t* hlm_req)
{
	nand_params_t* np = NULL;
	uint32_t kpg_loop = 0;

	np = &bdi->ptr_bdbm_params->nand;

	/* temp */
	if (hlm_req->org_pptr_kpgs) {
		hlm_req->pptr_kpgs = hlm_req->org_pptr_kpgs;
		hlm_req->kpg_flags = hlm_req->org_kpg_flags;
		hlm_req->lpa--;
		hlm_req->len++;
	}
	/* end */

	/* free or unmap pages */
	if (hlm_req->kpg_flags != NULL && hlm_req->pptr_kpgs != NULL) {
		for (kpg_loop = 0; kpg_loop < hlm_req->len; kpg_loop++) {
			if (hlm_req->kpg_flags[kpg_loop] == MEMFLAG_KMAP_PAGE_DONE) {
				/* ok. do nothing */
				if (hlm_req->pptr_kpgs[0]) {
					free (hlm_req->pptr_kpgs[0]);
					hlm_req->pptr_kpgs[0] = NULL;
				}
			} else if (hlm_req->kpg_flags[kpg_loop] != MEMFLAG_NOT_SET) {
				/* what??? */
				bdbm_error ("invalid flags (kpg_flags[%u]=%u)", 
					kpg_loop, 
					hlm_req->kpg_flags[kpg_loop]);
			}
		}
	}

	/* release other stuff */
	if (hlm_req->kpg_flags != NULL) 
		bdbm_free_atomic (hlm_req->kpg_flags);
	if (hlm_req->pptr_kpgs != NULL) 
		bdbm_free_atomic (hlm_req->pptr_kpgs);

	/* temp */
	bdbm_free (hlm_req->ptr_host_req);
	/* end */
	bdbm_free_atomic (hlm_req);
}

uint32_t host_user_open (bdbm_drv_info_t* bdi)
{
	uint32_t ret;
	bdbm_host_block_private_t* p;

	/* create a private data structure */
	if ((p = (bdbm_host_block_private_t*)bdbm_malloc_atomic
			(sizeof (bdbm_host_block_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return 1;
	}
	p->nr_host_reqs = 0;
	bdbm_spin_lock_init (&p->lock); 
	bdbm_mutex_init (&p->host_lock);

	bdi->ptr_host_inf->ptr_private = (void*)p;

	return 0;
}

void host_user_close (bdbm_drv_info_t* bdi)
{
	unsigned long flags;
	bdbm_host_block_private_t* p = NULL;

	p = (bdbm_host_block_private_t*)BDBM_HOST_PRIV(bdi);

	/* wait for host reqs to finish */
	bdbm_msg ("wait for host reqs to finish");
	for (;;) {
		bdbm_spin_lock_irqsave (&p->lock, flags);
		if (p->nr_host_reqs == 0) {
			bdbm_spin_unlock_irqrestore (&p->lock, flags);
			break;
		}
		bdbm_spin_unlock_irqrestore (&p->lock, flags);

		/*bdbm_msg ("p->nr_host_reqs = %llu", p->nr_host_reqs);*/

		/*sleep (1);*/
		bdbm_thread_msleep (1);
	}

	bdbm_mutex_free (&p->host_lock);

	/* free private */
	bdbm_free_atomic (p);
}

void host_user_make_req (
	bdbm_drv_info_t* bdi, 
	void *bio)
{
	unsigned long flags;
	nand_params_t* np = NULL;
	bdbm_hlm_req_t* hlm_req = NULL;
	bdbm_host_block_private_t* p = NULL;
	bdbm_host_req_t* host_req = (bdbm_host_req_t*)bio;

	np = &bdi->ptr_bdbm_params->nand;
	p = (bdbm_host_block_private_t*)BDBM_HOST_PRIV(bdi);

	bdbm_mutex_lock (&p->host_lock);

	/* create a hlm_req using a bio */
	if ((hlm_req = __host_block_create_hlm_req (bdi, host_req)) == NULL) {
		bdbm_spin_unlock_irqrestore (&p->lock, flags);
		bdbm_error ("the creation of hlm_req failed");
		return;
	}

	/* if success, increase # of host reqs */
	bdbm_spin_lock_irqsave (&p->lock, flags);
	p->nr_host_reqs++;
	bdbm_spin_unlock_irqrestore (&p->lock, flags);

	/* NOTE: it would be possible that 'hlm_req' becomes NULL 
	 * if 'bdi->ptr_hlm_inf->make_req' is success. */
	if (bdi->ptr_hlm_inf->make_req (bdi, hlm_req) != 0) {
		bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");

		/* decreate # of reqs */
		bdbm_spin_lock_irqsave (&p->lock, flags);
		if (p->nr_host_reqs > 0)
			p->nr_host_reqs--;
		else
			bdbm_error ("p->nr_host_reqs is 0");
		bdbm_spin_unlock_irqrestore (&p->lock, flags);

		/* finish a bio */
		__host_block_delete_hlm_req (bdi, hlm_req);
	}

	bdbm_mutex_unlock (&p->host_lock);
}

void host_user_end_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hlm_req)
{
	uint32_t ret;
	unsigned long flags;
	/*bdbm_host_req_t* host_req = NULL;*/
	bdbm_host_block_private_t* p = NULL;

	/* get a bio from hlm_req */
	p = (bdbm_host_block_private_t*)BDBM_HOST_PRIV(bdi);
	ret = hlm_req->ret;

	/* destroy hlm_req */
	__host_block_delete_hlm_req (bdi, hlm_req);

	/* decreate # of reqs */
	bdbm_spin_lock_irqsave (&p->lock, flags);
	if (p->nr_host_reqs > 0)
		p->nr_host_reqs--;
	else
		bdbm_bug_on (1);
	bdbm_spin_unlock_irqrestore (&p->lock, flags);
}

