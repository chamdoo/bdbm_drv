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

#include "blkio_stub.h"
#include "blkio_proxy_ioctl.h"
#include "hlm_reqs_pool.h"


#define NEW_HLM

bdbm_host_inf_t _blkio_stub_inf = {
	.ptr_private = NULL,
	.open = blkio_stub_open,
	.close = blkio_stub_close,
	.make_req = blkio_stub_make_req,
	.end_req = blkio_stub_end_req,
};

typedef struct {
	int fd;
	int stop;
	bdbm_blkio_proxy_req_t* mmap_reqs;
	bdbm_thread_t* host_stub_thread; /* polling the blockio proxy */
	atomic_t nr_host_reqs;
	bdbm_mutex_t host_lock;

#ifdef NEW_HLM
	/* TEMP */
	bdbm_hlm_reqs_pool_t* hlm_reqs_pool;
	/* TEMP */
#endif
} bdbm_blkio_stub_private_t;


int __host_proxy_stub_thread (void* arg) 
{
	bdbm_drv_info_t* bdi = (bdbm_drv_info_t*)arg;
	bdbm_device_params_t* np = (bdbm_device_params_t*)BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_host_inf_t* host_inf = (bdbm_host_inf_t*)BDBM_GET_HOST_INF (bdi);
	bdbm_blkio_stub_private_t* p = (bdbm_blkio_stub_private_t*)BDBM_HOST_PRIV (bdi);
	struct pollfd fds[1];
	int ret, i, j, sent;

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
			bdbm_blkio_proxy_req_t* proxy_req = NULL;

			sent = 0;
			for (i = 0; i < BDBM_PROXY_MAX_REQS; i++) {
				/* fetch the outstanding request from mmap */
				proxy_req = &p->mmap_reqs[i];
				bdbm_bug_on (proxy_req->id != i);

				/* are there any requests to send to the device? */
				if (proxy_req->stt == REQ_STT_KERN_SENT) {
					proxy_req->stt = REQ_STT_USER_PROG;
					/* setup blkio_req */
					for (j = 0; j < proxy_req->blkio_req.bi_bvec_cnt; j++)
						proxy_req->blkio_req.bi_bvec_ptr[j] = proxy_req->bi_bvec_ptr[j];
					/* send the request */
					host_inf->make_req (bdi, &proxy_req->blkio_req);
					sent++;
				}
			}

			/* how many outstanding requests were sent? */
			if (sent == 0) {
				bdbm_warning ("hmm... this is an impossible case");
			}
		}
	}

	pthread_exit (0);

	return 0;
}

uint32_t blkio_stub_open (bdbm_drv_info_t* bdi)
{
	bdbm_blkio_stub_private_t* p = NULL;
	int size;

	/* create a private data for host_proxy */
	if ((p = bdbm_malloc (sizeof (bdbm_blkio_stub_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc () failed");
		return 1;
	}
	p->fd = -1;
	p->stop = 0;
	p->mmap_reqs = NULL;
	atomic_set (&p->nr_host_reqs, 0);
	bdbm_mutex_init (&p->host_lock);
	bdi->ptr_host_inf->ptr_private = (void*)p;

	/* connect to blkio_proxy */
	if ((p->fd = open (BDBM_BLOCKIO_PROXY_IOCTL_DEVNAME, O_RDWR)) < 0) {
		bdbm_error ("open () failed (ret = %d)\n", p->fd);
		return 1;
	}

	/* create mmap_reqs */
	size = sizeof (bdbm_blkio_proxy_req_t) * BDBM_PROXY_MAX_REQS;
	if ((p->mmap_reqs = mmap (NULL,
			size,
			PROT_READ | PROT_WRITE, 
			MAP_SHARED, 
			p->fd, 0)) == NULL) {
		bdbm_warning ("bdbm_dm_proxy_mmap () failed");
		return 1;
	}

#ifdef NEW_HLM
	/* TEMP */
	if ((p->hlm_reqs_pool = bdbm_hlm_reqs_pool_create (
			bdi->parm_dev.page_main_size, 
			bdi->parm_dev.page_main_size)) == NULL) {
		bdbm_warning ("bdbm_hlm_reqs_pool_create () failed");
		return 1;
	}
	/* TEMP */
#endif

	/* run a thread to poll the blockio proxy */
	if ((p->host_stub_thread = bdbm_thread_create (
			__host_proxy_stub_thread, bdi, "__host_proxy_stub_thread")) == NULL) {
		bdbm_warning ("bdbm_thread_create failed");
		return 1;
	}
	bdbm_thread_run (p->host_stub_thread);

	return 0;
}

void blkio_stub_close (bdbm_drv_info_t* bdi)
{
	bdbm_blkio_stub_private_t* p = BDBM_HOST_PRIV (bdi); 

	/* stop the blkio_stub thread */
	p->stop = 1;
	bdbm_thread_stop (p->host_stub_thread);

	/* wait until requests to finish */
	if (atomic_read (&p->nr_host_reqs) > 0) {
		bdbm_thread_yield ();
	}

#ifdef NEW_HLM
	/* TEMP */
	if (p->hlm_reqs_pool) {
		bdbm_hlm_reqs_pool_destroy (p->hlm_reqs_pool);
	}
	/* TEMP */
#endif

	/* close the blkio_proxy */
	if (p->fd >= 0) {
		close (p->fd);
	}

	/* free stub */
	bdbm_free (p);
}

static void __blkio_stub_finish (
	bdbm_drv_info_t* bdi, 
	bdbm_blkio_req_t* r)
{
	bdbm_blkio_stub_private_t* p = BDBM_HOST_PRIV(bdi);
	bdbm_blkio_proxy_req_t* proxy_req = (bdbm_blkio_proxy_req_t*)r;

	/* change the status of the request */
	proxy_req->stt = REQ_STT_USER_DONE;

	/* send a 'done' siganl to the proxy */
	ioctl (p->fd, BDBM_BLOCKIO_PROXY_IOCTL_DONE, &proxy_req->id);
}

#if OLD_HLM
static bdbm_hlm_req_t* __blkio_stub_create_hlm_trim_req (
	bdbm_drv_info_t* bdi, 
	bdbm_blkio_req_t* r)
{
	bdbm_hlm_req_t* hlm_req = NULL;
	bdbm_device_params_t* np = (bdbm_device_params_t*)BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_ftl_params* dp = (bdbm_ftl_params*)BDBM_GET_DRIVER_PARAMS (bdi);
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
		hlm_req->lpa = r->bi_offset / nr_secs_per_fp;
		hlm_req->len = r->bi_size / nr_secs_per_fp;
		if (hlm_req->len == 0) 
			hlm_req->len = 1;
	} else {
		hlm_req->lpa = (r->bi_offset + nr_secs_per_fp - 1) / nr_secs_per_fp;
		if ((hlm_req->lpa * nr_secs_per_fp - r->bi_offset) > r->bi_size) {
			hlm_req->len = 0;
		} else {
			hlm_req->len = (r->bi_size - (hlm_req->lpa * nr_secs_per_fp - r->bi_offset)) / nr_secs_per_fp;
		}
	}
	hlm_req->nr_done_reqs = 0;
	hlm_req->kpg_flags = NULL;
	hlm_req->pptr_kpgs = NULL;	/* no data */
	hlm_req->ptr_host_req = (void*)r;
	hlm_req->ret = 0;

	/*bdbm_msg ("TRIM(ORG): LPA=%llu LEN=%llu (%llu %llu)", */
	/*hlm_req->lpa, hlm_req->len, r->bi_offset, r->bi_size);*/

	return hlm_req;
}

bdbm_hlm_req_t* __blkio_stub_create_hlm_req (
	bdbm_drv_info_t* bdi, 
	bdbm_blkio_req_t* r)
{
	bdbm_hlm_req_t* hlm_req = NULL;
	bdbm_device_params_t* np = (bdbm_device_params_t*)BDBM_GET_DEVICE_PARAMS (bdi);
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
	hlm_req->req_type = r->bi_rw;
	hlm_req->lpa = (r->bi_offset / nr_secs_per_fp);
	hlm_req->len = (r->bi_offset + r->bi_size + nr_secs_per_fp - 1) / nr_secs_per_fp - hlm_req->lpa;
	hlm_req->nr_done_reqs = 0;
	hlm_req->ptr_host_req = (void*)r;
	hlm_req->ret = 0;
	bdbm_stopwatch_start (&hlm_req->sw);
	/*bdbm_spin_lock_init (&hlm_req->lock);*/

	/*bdbm_msg ("RW(ORG): LPA=%llu LEN=%llu (%llu %llu)", */
	/*hlm_req->lpa, hlm_req->len, r->bi_offset, r->bi_size);*/

	if ((hlm_req->pptr_kpgs = (uint8_t**)bdbm_malloc
			(sizeof(uint8_t*) * hlm_req->len * nr_kp_per_fp)) == NULL) {
		bdbm_error ("bdbm_malloc failed"); 
		bdbm_free (hlm_req);
		return NULL;
	}
	if ((hlm_req->kpg_flags = (uint8_t*)bdbm_malloc
			(sizeof(uint8_t) * hlm_req->len * nr_kp_per_fp)) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		bdbm_free (hlm_req->pptr_kpgs);
		bdbm_free (hlm_req);
		return NULL;
	}

	/* get the data from bio */
	for (i = 0; i < r->bi_bvec_cnt; i++) {
next_kpg:
 		/* assign a new page */
		if ((hlm_req->lpa * nr_kp_per_fp + kpg_loop) != (r->bi_offset + bvec_offset) / nr_secs_per_kp) {
			hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)bdbm_malloc (KERNEL_PAGE_SIZE);
			hlm_req->kpg_flags[kpg_loop] = MEMFLAG_FRAG_PAGE;
			/*bdbm_msg ("[OLD] H-HOLE: %llu <= %llu", kpg_loop, i);*/
			kpg_loop++;
			/*bdbm_msg ("MEMFLAG_FRAG_PAGE is observed (type-1)");*/
			goto next_kpg;
		}

		hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)r->bi_bvec_ptr[i];
		hlm_req->kpg_flags[kpg_loop] = MEMFLAG_KMAP_PAGE;
		/*bdbm_msg ("[OLD] M-DATA: %llu <= %llu", kpg_loop, i);*/

		bvec_offset += nr_secs_per_kp;
		kpg_loop++;
	}

	/* get additional free pages if necessary */
	while (kpg_loop < hlm_req->len * nr_kp_per_fp) {
		hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)bdbm_malloc (KERNEL_PAGE_SIZE);
		hlm_req->kpg_flags[kpg_loop] = MEMFLAG_FRAG_PAGE;
		/*bdbm_msg ("[OLD] T-HOLE: %llu <= %llu", kpg_loop, i);*/
		kpg_loop++;
	}

	return hlm_req;
}

void __blkio_stub_delete_hlm_req (
	bdbm_drv_info_t* bdi, 
	bdbm_hlm_req_t* hlm_req)
{
	bdbm_device_params_t* np = (bdbm_device_params_t*)BDBM_GET_DEVICE_PARAMS (bdi);
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
#endif

void blkio_stub_make_req (bdbm_drv_info_t* bdi, void* bio)
{
#ifdef OLD_HLM
	bdbm_blkio_stub_private_t* p = (bdbm_blkio_stub_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_blkio_req_t* r = (bdbm_blkio_req_t*)bio;
	bdbm_hlm_req_t* hlm_req = NULL;

	/*bdbm_msg ("offset: %llu size: %llu", r->bi_offset, r->bi_size);*/

	/* create a hlm_req using a bio */
	if (r->bi_rw == REQTYPE_TRIM) {
		if ((hlm_req = __blkio_stub_create_hlm_trim_req (bdi, r)) == NULL) {
			bdbm_mutex_unlock (&p->host_lock);
			bdbm_error ("the creation of hlm_req failed");
			return;
		}
	} else {
		if ((hlm_req = __blkio_stub_create_hlm_req (bdi, r)) == NULL) {
			bdbm_mutex_unlock (&p->host_lock);
			bdbm_error ("the creation of hlm_req failed");
			return;
		}
	}

#ifdef NEW_HLM
	{
		bdbm_hlm_req_t* temp_r = NULL;
		int i = 0, j = 0, k = 0, pos;

		if ((temp_r = bdbm_hlm_reqs_pool_alloc_item (p->hlm_reqs_pool)) == NULL) {
			bdbm_track ();
			return;
		}
		if (bdbm_hlm_reqs_pool_build_req (p->hlm_reqs_pool, temp_r, r) != 0) {
			bdbm_track ();
			return;
		}
		hlm_req->temp_hlm = (void*)temp_r;

		/* compare old with new */
		k = bdi->parm_dev.page_main_size / KERNEL_PAGE_SIZE;

		if (hlm_req->req_type != REQTYPE_TRIM) {
			if (hlm_req->len != temp_r->nr_llm_reqs) {
				bdbm_msg ("hlm_req->len != temp_r->nr_llm_reqs (%llu != %llu)", hlm_req->len, temp_r->nr_llm_reqs);
			}
			pos = 0;
			for (i = 0; i < hlm_req->len; i++) {
				for (j = 0; j < k; j++) {
					if (hlm_req->kpg_flags[pos] == MEMFLAG_FRAG_PAGE) {
						if (temp_r->llm_reqs[i].fmain.kp_stt[j] != KP_STT_HOLE) {
							bdbm_msg ("[ERROR] HOLE is different (%llx %llx) (%llu %llu)", 
								hlm_req->kpg_flags[pos], 
								temp_r->llm_reqs[i].fmain.kp_stt[j],
								i, j);
						}
					}
					if (hlm_req->kpg_flags[pos] == MEMFLAG_KMAP_PAGE) {
						if (temp_r->llm_reqs[i].fmain.kp_stt[j] != KP_STT_DATA) {
							bdbm_msg ("[ERROR] DATA is different (%llx %llx) (%llu %llu)", 
								hlm_req->kpg_flags[pos], 
								temp_r->llm_reqs[i].fmain.kp_stt[j],
								i, j);
						}
					}
					pos++;
				}
			}
		} else {
			if (hlm_req->len != temp_r->trim_len) {
				bdbm_msg ("[ERROR] hlm_req->len != temp_r->trim_len (%lld %lld)",
					hlm_req->len, temp_r->trim_len);
			}
			if (hlm_req->lpa != temp_r->trim_lpa) {
				bdbm_msg ("[ERROR] hlm_req->lpa != temp_r->trim_lpa (%lld %lld)",
					hlm_req->lpa, temp_r->trim_lpa);
			}
		}
	}
	/* TEMP */
#endif

	/* if success, increase # of host reqs */
	atomic_inc (&p->nr_host_reqs);

	/* NOTE: it would be possible that 'hlm_req' becomes NULL 
	 * if 'bdi->ptr_hlm_inf->make_req' is success. */
	if (bdi->ptr_hlm_inf->make_req (bdi, hlm_req) != 0) {
		/* oops! something wrong */
		bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");

		/* cancel the request */
		__blkio_stub_delete_hlm_req (bdi, hlm_req);
		__blkio_stub_finish (bdi, r);
		atomic_dec (&p->nr_host_reqs);
	}

	/*bdbm_msg ("");*/
#endif

	/* NEW_HLM */
	bdbm_blkio_stub_private_t* p = (bdbm_blkio_stub_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_blkio_req_t* br = (bdbm_blkio_req_t*)bio;
	bdbm_hlm_req_t* hr = NULL;

	/*bdbm_msg ("type: %llx offset: %llu size: %llu", br->bi_rw, br->bi_offset, br->bi_size);*/

	/* get a free hlm_req from the hlm_reqs_pool */
	if ((hr = bdbm_hlm_reqs_pool_alloc_item (p->hlm_reqs_pool)) == NULL) {
		bdbm_error ("bdbm_hlm_reqs_pool_alloc_item () failed");
		bdbm_bug_on (1);
		return;
	}

	/* build hlm_req with bio */
	if (bdbm_hlm_reqs_pool_build_req (p->hlm_reqs_pool, hr, br) != 0) {
		bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
		bdbm_bug_on (1);
		return;
	}

	/* if success, increase # of host reqs */
	atomic_inc (&p->nr_host_reqs);

	/* NOTE: it would be possible that 'hlm_req' becomes NULL 
	 * if 'bdi->ptr_hlm_inf->make_req' is success. */
	if (bdi->ptr_hlm_inf->make_req (bdi, hr) != 0) {
		/* oops! something wrong */
		bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");

		/* cancel the request */
		/*__blkio_stub_delete_hlm_req (bdi, hlm_req);*/
		bdbm_hlm_reqs_pool_free_item (p->hlm_reqs_pool, hr);
		__blkio_stub_finish (bdi, br);
		atomic_dec (&p->nr_host_reqs);
	}

	/*bdbm_msg ("");*/
}

#ifdef OLD_HLM
void blkio_stub_end_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* req)
{
	bdbm_blkio_stub_private_t* p = (bdbm_blkio_stub_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_blkio_req_t* r = (bdbm_blkio_req_t*)req->ptr_host_req;

	/* finish the proxy request */
	__blkio_stub_finish (bdi, r);

	/* decreate # of reqs */
	atomic_dec (&p->nr_host_reqs);

	/* TEMP */
#ifdef NEW_HLM
	{
		bdbm_hlm_req_t* hlm_req = req->temp_hlm;
		bdbm_hlm_reqs_pool_free_item (p->hlm_reqs_pool, hlm_req);
	}
#endif
	/* TEMP */

	/* destroy hlm_req */
	__blkio_stub_delete_hlm_req (bdi, req);
}
#else
void blkio_stub_end_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* req)
{
	bdbm_blkio_stub_private_t* p = (bdbm_blkio_stub_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_blkio_req_t* r = (bdbm_blkio_req_t*)req->blkio_req;

	/* finish the proxy request */
	__blkio_stub_finish (bdi, r);

	/* decreate # of reqs */
	atomic_dec (&p->nr_host_reqs);

	/* destroy hlm_req */
	/*__blkio_stub_delete_hlm_req (bdi, req);*/
	bdbm_hlm_reqs_pool_free_item (p->hlm_reqs_pool, req);
}
#endif
