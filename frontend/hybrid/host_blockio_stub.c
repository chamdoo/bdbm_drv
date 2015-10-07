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
#include <errno.h>	/* strerr, errno */
#include <fcntl.h> /* O_RDWR */
#include <sys/mman.h> /* mmap */
#include <poll.h> /* poll */
#include <sys/ioctl.h> /* ioctl */

#include "bdbm_drv.h"
#include "debug.h"
#include "platform.h"
#include "params.h"
#include "utime.h"
#include "uthread.h"

#include "host_blockio_stub.h"
#include "host_blockio_proxy_ioctl.h"


bdbm_host_inf_t _host_blockio_stub_inf = {
	.ptr_private = NULL,
	.open = blockio_stub_open,
	.close = blockio_stub_close,
	.make_req = blockio_stub_make_req,
	.end_req = blockio_stub_end_req,
};

typedef struct {
	int fd;
	int stop;
	bdbm_blockio_proxy_req_t* mmap_reqs;
	bdbm_thread_t* host_stub_thread; /* polling the blockio proxy */
	atomic_t nr_host_reqs;
	bdbm_mutex_t host_lock;
} bdbm_blockio_stub_private_t;

int __host_proxy_stub_thread (void* arg) 
{
	bdbm_drv_info_t* bdi = (bdbm_drv_info_t*)arg;
	nand_params_t* np = (nand_params_t*)BDBM_GET_NAND_PARAMS (bdi);
	bdbm_host_inf_t* host_inf = (bdbm_host_inf_t*)BDBM_GET_HOST_INF (bdi);
	bdbm_blockio_stub_private_t* p = (bdbm_blockio_stub_private_t*)BDBM_HOST_PRIV (bdi);
	struct pollfd fds[1];
	int ret, i, sent;

	while (p->stop != 1) {
		bdbm_thread_yield ();

		/* prepare arguments for poll */
		fds[0].fd = p->fd;
		fds[0].events = POLLIN;

		/* call poll () with 3 seconds timout */
		ret = poll (fds, 1, 3000);	/* p->ps is shared by kernel, but it is only updated by kernel when poll () is called */

		/* timeout: continue to check the device status */
		if (ret == 0)
			continue;

		/* error: poll () returns error for some reasones */
		if (ret < 0) 
			continue;

		/* success */
		if (ret > 0) {
			bdbm_blockio_proxy_req_t* r = NULL;
			int* test = NULL;
			static int bug = 0;
			for (i = 0; i < 31; i++) {
				/* fetch the outstanding request from mmap */
				r = &p->mmap_reqs[i];
				bdbm_bug_on (r->id != i);
				/* send the requests to host stub */
				if (r->stt == REQ_STT_KERN_SENT) {
					r->stt = REQ_STT_USER_PROG;
					/*bdbm_msg ("[user-1] send proxy requests to the host stub: %llu %llu", r->bi_sector, r->bi_size);*/
					host_inf->make_req (bdi, (void*)r);
					sent++;
				}
			}

			/* how many outstanding requests were sent? */
			if (sent == 0) {
				bdbm_warning ("hmm... this is an impossible case");
			}

			if (bug > 10000) {
				*test = 0x00;
			}
			bug++;
		}
	}

	pthread_exit (0);

	return 0;
}

uint32_t blockio_stub_open (bdbm_drv_info_t* bdi)
{
	bdbm_blockio_stub_private_t* p = NULL;
	int size;

	/* create a private data for host_proxy */
	if ((p = bdbm_malloc (sizeof (bdbm_blockio_stub_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc () failed");
		return 1;
	}
	p->fd = -1;
	p->stop = 0;
	p->mmap_reqs = NULL;
	atomic_set (&p->nr_host_reqs, 0);
	bdbm_mutex_init (&p->host_lock);
	bdi->ptr_host_inf->ptr_private = (void*)p;

	/* connect to blockio_proxy */
	if ((p->fd = open (BDBM_BLOCKIO_PROXY_IOCTL_DEVNAME, O_RDWR)) < 0) {
		bdbm_error ("open () failed (ret = %d)\n", p->fd);
		return 1;
	}

	/* create mmap_reqs */
	size = sizeof (bdbm_blockio_proxy_req_t) * 31;
	if ((p->mmap_reqs = mmap (NULL,
			size,
			PROT_READ | PROT_WRITE, 
			MAP_SHARED, 
			p->fd, 0)) == NULL) {
		bdbm_warning ("bdbm_dm_proxy_mmap () failed");
		return 1;
	}

	/* run a thread to poll the blockio proxy */
	if ((p->host_stub_thread = bdbm_thread_create (
			__host_proxy_stub_thread, bdi, "__host_proxy_stub_thread")) == NULL) {
		bdbm_warning ("bdbm_thread_create failed");
		return 1;
	}
	bdbm_thread_run (p->host_stub_thread);

	return 0;
}

void blockio_stub_close (bdbm_drv_info_t* bdi)
{
	bdbm_blockio_stub_private_t* p = BDBM_HOST_PRIV (bdi); 

	/* stop the blockio_stub thread */
	p->stop = 1;
	bdbm_thread_stop (p->host_stub_thread);

	if (atomic_read (&p->nr_host_reqs) > 0) {
		bdbm_thread_yield ();
	}

	/* close the blockio_proxy */
	close (p->fd);

	/* free stub */
	bdbm_free (p);
}

static void __blockio_stub_finish (
	bdbm_drv_info_t* bdi, 
	bdbm_blockio_proxy_req_t* proxy_req)
{
	bdbm_blockio_stub_private_t* p = (bdbm_blockio_stub_private_t*)BDBM_HOST_PRIV(bdi);

	/* change the status of the request */
	proxy_req->stt = REQ_STT_USER_DONE;

	/* send a 'done' siganl to the proxy */
	ioctl (p->fd, BDBM_BLOCKIO_PROXY_IOCTL_DONE, &proxy_req->id);
}

static bdbm_hlm_req_t* __blockio_stub_create_hlm_trim_req (
	bdbm_drv_info_t* bdi, 
	bdbm_blockio_proxy_req_t* proxy_req)
{
	bdbm_hlm_req_t* hlm_req = NULL;
	nand_params_t* np = (nand_params_t*)BDBM_GET_NAND_PARAMS (bdi);
	driver_params_t* dp = (driver_params_t*)BDBM_GET_DRIVER_PARAMS (bdi);
	uint64_t nr_secs_per_fp = np->page_main_size / KERNEL_SECTOR_SIZE;

	/* create bdbm_hm_req_t */
	if ((hlm_req = (bdbm_hlm_req_t*)bdbm_malloc (sizeof (bdbm_hlm_req_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return NULL;
	}

	/* make a high-level request for TRIM */
	hlm_req->req_type = REQTYPE_TRIM;
	bdbm_stopwatch_start (&hlm_req->sw);
	
	if (dp->mapping_type == MAPPING_POLICY_SEGMENT) {
		hlm_req->lpa = proxy_req->bi_sector / nr_secs_per_fp;
		hlm_req->len = proxy_req->bi_size / nr_secs_per_fp;
		if (hlm_req->len == 0) 
			hlm_req->len = 1;
	} else {
		hlm_req->lpa = (proxy_req->bi_sector + nr_secs_per_fp - 1) / nr_secs_per_fp;
		if ((hlm_req->lpa * nr_secs_per_fp - proxy_req->bi_sector) > proxy_req->bi_size) {
			hlm_req->len = 0;
		} else {
			hlm_req->len = (proxy_req->bi_size - (hlm_req->lpa * nr_secs_per_fp - proxy_req->bi_sector)) / nr_secs_per_fp;
		}
	}
	hlm_req->nr_done_reqs = 0;
	hlm_req->kpg_flags = NULL;
	hlm_req->pptr_kpgs = NULL;	/* no data */
	hlm_req->ptr_host_req = (void*)proxy_req;
	hlm_req->ret = 0;

	return hlm_req;
}

bdbm_hlm_req_t* __blockio_stub_create_hlm_req (
	bdbm_drv_info_t* bdi, 
	bdbm_blockio_proxy_req_t* proxy_req)
{
	bdbm_hlm_req_t* hlm_req = NULL;
	nand_params_t* np = (nand_params_t*)BDBM_GET_NAND_PARAMS (bdi);
	uint64_t nr_secs_per_fp = np->page_main_size / KERNEL_SECTOR_SIZE;
	uint64_t nr_secs_per_kp = KERNEL_PAGE_SIZE / KERNEL_SECTOR_SIZE;
	uint32_t nr_kp_per_fp = np->page_main_size / KERNEL_PAGE_SIZE;
	uint32_t bvec_offset = 0;
	uint32_t kpg_loop = 0;
	uint64_t i;

	/* create the hlm_req */
	if ((hlm_req = (bdbm_hlm_req_t*)bdbm_malloc (sizeof (bdbm_hlm_req_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return NULL;
	}

	/* build the hlm_req */
	hlm_req->req_type = proxy_req->bi_rw;
	hlm_req->lpa = (proxy_req->bi_sector / nr_secs_per_fp);
	hlm_req->len = (proxy_req->bi_sector + proxy_req->bi_size + nr_secs_per_fp - 1) / nr_secs_per_fp - hlm_req->lpa;
	hlm_req->nr_done_reqs = 0;
	hlm_req->ptr_host_req = (void*)proxy_req;
	hlm_req->ret = 0;
	bdbm_stopwatch_start (&hlm_req->sw);
	bdbm_spin_lock_init (&hlm_req->lock);

	if ((hlm_req->pptr_kpgs = (uint8_t**)bdbm_malloc
			(sizeof(uint8_t*) * hlm_req->len)) == NULL) {
		bdbm_error ("bdbm_malloc failed"); 
		bdbm_free (hlm_req);
		return NULL;
	}
	if ((hlm_req->kpg_flags = (uint8_t*)bdbm_malloc
			(sizeof(uint8_t) * hlm_req->len)) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		bdbm_free (hlm_req->pptr_kpgs);
		bdbm_free (hlm_req);
		return NULL;
	}

	/* get the data from bio */
	for (i = 0; i < proxy_req->bi_bvec_cnt; i++) {
next_kpg:
 		/* assign a new page */
		if ((hlm_req->lpa * nr_kp_per_fp + kpg_loop) != (proxy_req->bi_sector + bvec_offset) / nr_secs_per_kp) {
			hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)bdbm_malloc (KERNEL_PAGE_SIZE);
			hlm_req->kpg_flags[kpg_loop] = MEMFLAG_FRAG_PAGE;
			kpg_loop++;
			bdbm_msg ("MEMFLAG_FRAG_PAGE is observed (type-1)");
			goto next_kpg;
		}

		hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)proxy_req->bi_bvec_data[i];
		hlm_req->kpg_flags[kpg_loop] = MEMFLAG_KMAP_PAGE;
		bvec_offset += nr_secs_per_kp;
		kpg_loop++;
	}

	/* get additional free pages if necessary */
	while (kpg_loop < hlm_req->len * nr_kp_per_fp) {
		hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)bdbm_malloc (KERNEL_PAGE_SIZE);
		hlm_req->kpg_flags[kpg_loop] = MEMFLAG_FRAG_PAGE;
		kpg_loop++;
	}

	return hlm_req;
}

void __blockio_stub_delete_hlm_req (
	bdbm_drv_info_t* bdi, 
	bdbm_hlm_req_t* hlm_req)
{
	nand_params_t* np = (nand_params_t*)BDBM_GET_NAND_PARAMS (bdi);
	uint32_t nr_kp_per_fp = np->page_main_size / KERNEL_PAGE_SIZE;
	uint32_t kpg_loop = 0;

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
		for (kpg_loop = 0; kpg_loop < hlm_req->len * nr_kp_per_fp; kpg_loop++) {
			if (hlm_req->kpg_flags[kpg_loop] == MEMFLAG_FRAG_PAGE_DONE) {
				bdbm_free (hlm_req->pptr_kpgs[kpg_loop]);
			} else if (hlm_req->kpg_flags[kpg_loop] == MEMFLAG_KMAP_PAGE_DONE) {
			} else if (hlm_req->kpg_flags[kpg_loop] != MEMFLAG_NOT_SET) {
				bdbm_error ("invalid flags (kpg_flags[%u]=%u)", kpg_loop, hlm_req->kpg_flags[kpg_loop]);
			}
		}
	}

	/* release other stuff */
	if (hlm_req->kpg_flags != NULL) 
		bdbm_free (hlm_req->kpg_flags);
	if (hlm_req->pptr_kpgs != NULL) 
		bdbm_free (hlm_req->pptr_kpgs);
	bdbm_free (hlm_req);
}

void blockio_stub_make_req (bdbm_drv_info_t* bdi, void* bio)
{
	bdbm_blockio_stub_private_t* p = (bdbm_blockio_stub_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_blockio_proxy_req_t* proxy_req = (bdbm_blockio_proxy_req_t*)bio;
	bdbm_hlm_req_t* hlm_req = NULL;

	/* create a hlm_req using a bio */
	if (proxy_req->bi_rw == REQTYPE_TRIM) {
		/*bdbm_msg ("[user-2] make hlm_trim_req");*/
		if ((hlm_req = __blockio_stub_create_hlm_trim_req (bdi, proxy_req)) == NULL) {
			bdbm_mutex_unlock (&p->host_lock);
			bdbm_error ("the creation of hlm_req failed");
			return;
		}
	} else {
		/*bdbm_msg ("[user-2] make hlm_trim_req");*/
		if ((hlm_req = __blockio_stub_create_hlm_req (bdi, proxy_req)) == NULL) {
			bdbm_mutex_unlock (&p->host_lock);
			bdbm_error ("the creation of hlm_req failed");
			return;
		}
	}

	/* if success, increase # of host reqs */
	atomic_inc (&p->nr_host_reqs);

	/* NOTE: it would be possible that 'hlm_req' becomes NULL 
	 * if 'bdi->ptr_hlm_inf->make_req' is success. */
	/*bdbm_msg ("[user-1] %llu %llu", proxy_req->bi_sector, proxy_req->bi_size);*/
	if (bdi->ptr_hlm_inf->make_req (bdi, hlm_req) != 0) {
		/* oops! something wrong */
		bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");

		/* cancel the request */
		__blockio_stub_delete_hlm_req (bdi, hlm_req);
		__blockio_stub_finish (bdi, proxy_req);
		atomic_dec (&p->nr_host_reqs);
	}
}

void blockio_stub_end_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* req)
{
	bdbm_blockio_stub_private_t* p = (bdbm_blockio_stub_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_blockio_proxy_req_t* proxy_req = (bdbm_blockio_proxy_req_t*)req->ptr_host_req;

	/*bdbm_msg ("[user-2] %llu %llu", proxy_req->bi_sector, proxy_req->bi_size); */

	/* finish the proxy request */
	__blockio_stub_finish (bdi, proxy_req);

	/* decreate # of reqs */
	atomic_dec (&p->nr_host_reqs);

	/* destroy hlm_req */
	__blockio_stub_delete_hlm_req (bdi, req);
}

