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

#if defined(KERNEL_MODE)
#include <linux/module.h>
#include <linux/blkdev.h>

#elif defined(USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "debug.h"
#include "params.h"
#include "bdbm_drv.h"
#include "hlm_nobuf.h"
#include "hlm_dftl.h"
#include "uthread.h"

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"
#include "queue/queue.h"


/* interface for hlm_dftl */
bdbm_hlm_inf_t _hlm_dftl_inf = {
	.ptr_private = NULL,
	.create = hlm_dftl_create,
	.destroy = hlm_dftl_destroy,
	.make_req = hlm_dftl_make_req,
	.end_req = hlm_dftl_end_req,
};

/* data structures for hlm_dftl */
typedef struct {
	bdbm_ftl_inf_t* ftl;	/* for hlm_nobuff (it must be on top of this structure) */

	/* for thread management */
	bdbm_queue_t* q;
	bdbm_thread_t* hlm_thread;
	bdbm_mutex_t ftl_lock;
} bdbm_hlm_dftl_private_t;

/* kernel thread for _llm_q */
int __hlm_dftl_thread (void* arg)
{
	bdbm_drv_info_t* bdi = (bdbm_drv_info_t*)arg;
	bdbm_hlm_dftl_private_t* p = (bdbm_hlm_dftl_private_t*)BDBM_HLM_PRIV(bdi);
	bdbm_hlm_req_t* r = NULL;

	for (;;) {
		/* Go to sleep if there are not requests in Q */
		if (bdbm_queue_is_all_empty (p->q)) {
			if (bdbm_thread_schedule (p->hlm_thread) == SIGKILL) {
				break;
			}
		}

		while (!bdbm_queue_is_empty (p->q, 0)) {
			if ((r = (bdbm_hlm_req_t*)bdbm_queue_dequeue (p->q, 0)) != NULL) {
				int loop = 0;
				bdbm_llm_req_t* mr = NULL;

				bdbm_mutex_lock (&p->ftl_lock);
				/* STEP1: read missing mapping entries */
				for (loop = 0; loop < r->len; loop++) {
					/* check the availability of mapping entries again */
					if (p->ftl->check_mapblk (bdi, r->lpa + loop) == 0)
						continue;

					/* fetch mapping entries to DRAM from Flash */
					if ((mr = p->ftl->prepare_mapblk_load (bdi, r->lpa + loop)) == NULL)
						continue;

					/* send read requets to llm */
					bdbm_mutex_lock (mr->done);
					bdi->ptr_llm_inf->make_req (bdi, mr);

					/* wait for all jobs to finish */
					bdbm_mutex_lock (mr->done); 
					p->ftl->finish_mapblk_load (bdi, mr);
				}

				/* STEP2: send origianl requests to llm */
				/*bdbm_msg ("[hlm-Q-2] Q to llm");*/
				if (hlm_nobuf_make_req (bdi, r)) {
					/* if it failed, we directly call 'ptr_host_inf->end_req' */
					bdi->ptr_host_inf->end_req (bdi, r);
					bdbm_warning ("oops! make_req failed");
					/* [CAUTION] r is now NULL */
				}
				bdbm_mutex_unlock (&p->ftl_lock);

				/* STEP3: give a chance for incoming requests to be served */
				bdbm_thread_yield (); 

				/* STEP4: evict mapping entries if there is not enough DRAM space */
				bdbm_mutex_lock (&p->ftl_lock);
				for (;;) {
					/* drop mapping enries to Flash */
					if ((mr = p->ftl->prepare_mapblk_eviction (bdi)) == NULL)
						break;

					/* send a req to llm */
					bdbm_mutex_lock (mr->done);
					bdi->ptr_llm_inf->make_req (bdi, mr);

					/* wait until it finishes */
					bdbm_mutex_lock (mr->done);
					p->ftl->finish_mapblk_eviction (bdi, mr);
				}
				bdbm_mutex_unlock (&p->ftl_lock);
			} else {
				bdbm_error ("r == NULL");
				bdbm_bug_on (1);
			}
		} 
	}

	return 0;
}

/* interface functions for hlm_dftl */
uint32_t hlm_dftl_create (bdbm_drv_info_t* bdi)
{
	bdbm_hlm_dftl_private_t* p;

	/* create private */
	if ((p = (bdbm_hlm_dftl_private_t*)bdbm_malloc_atomic
			(sizeof(bdbm_hlm_dftl_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return 1;
	}

	/* setup FTL function pointers */
	if ((p->ftl= BDBM_GET_FTL_INF (bdi)) == NULL) {
		bdbm_error ("ftl is not valid");
		return 1;
	}

	/* create a single queue */
	if ((p->q = bdbm_queue_create (1, INFINITE_QUEUE)) == NULL) {
		bdbm_error ("bdbm_queue_create failed");
		return -1;
	}

	bdbm_mutex_init (&p->ftl_lock);

	/* keep the private structure */
	bdi->ptr_hlm_inf->ptr_private = (void*)p;

	/* create & run a thread */
	if ((p->hlm_thread = bdbm_thread_create (
			__hlm_dftl_thread, bdi, "__hlm_dftl_thread")) == NULL) {
		bdbm_error ("kthread_create failed");
		return -1;
	}
	bdbm_thread_run (p->hlm_thread);

	return 0;
}

void hlm_dftl_destroy (bdbm_drv_info_t* bdi)
{
	bdbm_hlm_dftl_private_t* p = (bdbm_hlm_dftl_private_t*)bdi->ptr_hlm_inf->ptr_private;

	/* wait until Q becomes empty */
	while (!bdbm_queue_is_all_empty (p->q)) {
		bdbm_msg ("hlm items = %llu", bdbm_queue_get_nr_items (p->q));
		bdbm_thread_msleep (1);
	}

	bdbm_mutex_free (&p->ftl_lock);

	/* kill kthread */
	bdbm_thread_stop (p->hlm_thread);

	/* destroy queue */
	bdbm_queue_destroy (p->q);

	/* free priv */
	bdbm_free_atomic (p);
}

uint32_t hlm_dftl_make_req (
	bdbm_drv_info_t* bdi, 
	bdbm_hlm_req_t* r)
{
	uint32_t ret, i;
	uint32_t avail = 0;
	bdbm_hlm_dftl_private_t* p = (bdbm_hlm_dftl_private_t*)BDBM_HLM_PRIV(bdi);

	if (bdbm_queue_is_full (p->q)) {
		/* FIXME: wait unti queue has a enough room */
		bdbm_error ("it should not be happened!");
		bdbm_bug_on (1);
	} 

	/* see if mapping entries for hlm_req are available */
	bdbm_mutex_lock (&p->ftl_lock);

	/* see if foreground GC is needed or not */
	if (r->req_type == REQTYPE_WRITE && 
		p->ftl->is_gc_needed != NULL && 
		p->ftl->is_gc_needed (bdi)) {
		/* perform GC before sending requests */ 
		/*bdbm_msg ("start - gc");*/
		p->ftl->do_gc (bdi);
		/*bdbm_msg ("stop - gc");*/
	}

	for (i = 0; i < r->len; i++) {
		if ((avail = p->ftl->check_mapblk (bdi, r->lpa + i)) == 1)
			break;
	}

	/* handle hlm_req */
	if (avail == 0) {
		/* If all of the mapping entries are available, send a hlm_req to llm directly */
		if ((ret = hlm_nobuf_make_req (bdi, r))) {
			/* if it failed, we directly call 'ptr_host_inf->end_req' */
			bdi->ptr_host_inf->end_req (bdi, r);
			bdbm_warning ("oops! make_req failed");
			/* [CAUTION] r is now NULL */
		}
	} else {
		/* If some of the mapping entries are *not* available, put a hlm_req to queue. 
		 * This allows other incoming requests not to be affected by a hlm_req
		 * with missing mapping entries */
		if ((ret = bdbm_queue_enqueue (p->q, 0, (void*)r))) {
			bdbm_msg ("bdbm_queue_enqueue failed");
		}
	}

	bdbm_mutex_unlock (&p->ftl_lock);

	/* wake up thread if it sleeps */
	bdbm_thread_wakeup (p->hlm_thread);

	return ret;
}

void hlm_dftl_end_req (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r)
{
	if (r->done) {
		/* FIXME: r->done is set to not NULL for mapblk */
		/*bdbm_msg ("[hlm_dftl] end_req");*/
		bdbm_mutex_unlock (r->done);
		return;
	}

	hlm_nobuf_end_req (bdi, r);
}

