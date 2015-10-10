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

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"
#include "utime.h"


/* interface for hlm_nobuf */
bdbm_hlm_inf_t _hlm_nobuf_inf = {
	.ptr_private = NULL,
	.create = hlm_nobuf_create,
	.destroy = hlm_nobuf_destroy,
	.make_req = hlm_nobuf_make_req,
	.end_req = hlm_nobuf_end_req,
	/*.load = hlm_nobuf_load,*/
	/*.store = hlm_nobuf_store,*/
};

/* data structures for hlm_nobuf */
struct bdbm_hlm_nobuf_private {
	bdbm_ftl_inf_t* ptr_ftl_inf;
};


/* functions for hlm_nobuf */
uint32_t hlm_nobuf_create (bdbm_drv_info_t* bdi)
{
	struct bdbm_hlm_nobuf_private* p;

	/* create private */
	if ((p = (struct bdbm_hlm_nobuf_private*)bdbm_malloc_atomic
			(sizeof(struct bdbm_hlm_nobuf_private))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return 1;
	}

	/* setup FTL function pointers */
	if ((p->ptr_ftl_inf = BDBM_GET_FTL_INF (bdi)) == NULL) {
		bdbm_error ("ftl is not valid");
		return 1;
	}

	/* keep the private structure */
	bdi->ptr_hlm_inf->ptr_private = (void*)p;

	return 0;
}

void hlm_nobuf_destroy (bdbm_drv_info_t* bdi)
{
	struct bdbm_hlm_nobuf_private* p = (struct bdbm_hlm_nobuf_private*)BDBM_HLM_PRIV(bdi);

	/* free priv */
	bdbm_free_atomic (p);
}

uint32_t __hlm_nobuf_make_trim_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* ptr_hlm_req)
{
	bdbm_ftl_inf_t* ftl = (bdbm_ftl_inf_t*)BDBM_GET_FTL_INF(bdi);
	uint64_t i;

	for (i = 0; i < ptr_hlm_req->len; i++) {
		ftl->invalidate_lpa (bdi, ptr_hlm_req->lpa + i, 1);
	}

	return 0;
}

uint32_t __hlm_nobuf_get_req_type (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* ptr_hlm_req, uint32_t index)
{
	bdbm_ftl_params* dp = BDBM_GET_DRIVER_PARAMS(bdi);
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS(bdi);
	uint32_t nr_kp_per_fp, req_type, j;

	nr_kp_per_fp = np->page_main_size / KERNEL_PAGE_SIZE;	/* e.g., 2 = 8 KB / 4 KB */

	/* temp */
	if (dp->mapping_type == MAPPING_POLICY_SEGMENT)
		return ptr_hlm_req->req_type;
	/* end */

	req_type = ptr_hlm_req->req_type;
	if (ptr_hlm_req->req_type == REQTYPE_WRITE) {
		for (j = 0; j < nr_kp_per_fp; j++) {
			if (ptr_hlm_req->kpg_flags[index * nr_kp_per_fp + j] == MEMFLAG_FRAG_PAGE) {
				req_type = REQTYPE_RMW_READ;
				break;
			}
		}
	}

	return req_type;
}

uint32_t __hlm_nobuf_make_rw_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* ptr_hlm_req)
{
	bdbm_ftl_inf_t* ftl = BDBM_GET_FTL_INF(bdi);
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS(bdi);
	bdbm_llm_req_t** pptr_llm_req;
	uint32_t nr_kp_per_fp;
	uint32_t hlm_len;
	uint32_t ret = 0;
	uint32_t i;

	nr_kp_per_fp = np->page_main_size / KERNEL_PAGE_SIZE;	/* e.g., 2 = 8 KB / 4 KB */
	hlm_len = ptr_hlm_req->len;

	/* create a set of llm_req */
	if ((pptr_llm_req = (bdbm_llm_req_t**)bdbm_malloc_atomic
			(sizeof (bdbm_llm_req_t*) * ptr_hlm_req->len)) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return -1;
	}

	/* divide a hlm_req into multiple llm_reqs */
	for (i = 0; i < hlm_len; i++) {
		bdbm_llm_req_t* r = NULL;

		/* create a low-level request */
		if ((r = (bdbm_llm_req_t*)bdbm_malloc_atomic 
				(sizeof (bdbm_llm_req_t))) == NULL) {
			bdbm_error ("bdbm_malloc_atomic failed");
			goto fail;
		}
		pptr_llm_req[i] = r;
	
		/* setup llm_req */
		r->req_type = __hlm_nobuf_get_req_type (bdi, ptr_hlm_req, i);
		r->lpa = ptr_hlm_req->lpa + i;
		switch (r->req_type) {
		case REQTYPE_READ:
		case REQTYPE_META_READ:
			/* get the physical addr for reads */
			r->phyaddr = &r->phyaddr_r;
			if (ftl->get_ppa (bdi, r->lpa, r->phyaddr) != 0)
				r->req_type = REQTYPE_READ_DUMMY; /* reads for unwritten pages */
			break;
		case REQTYPE_RMW_READ:
			/* get the physical addr for original pages */
			r->phyaddr = &r->phyaddr_r;
			if (ftl->get_ppa (bdi, r->lpa, r->phyaddr) != 0)
				r->req_type = REQTYPE_WRITE;
			/* go ahead! */
		case REQTYPE_WRITE:
		case REQTYPE_META_WRITE:
			r->phyaddr = &r->phyaddr_w;
			/* get the physical addr where new page will be written */
			if (ftl->get_free_ppa (bdi, r->lpa, r->phyaddr) != 0) {
				bdbm_error ("`ftl->get_free_ppa' failed");
				goto fail;
			}
			if (ftl->map_lpa_to_ppa (bdi, r->lpa, r->phyaddr) != 0) {
				bdbm_error ("`ftl->map_lpa_to_ppa' failed");
				goto fail;
			}
			break;
		default:
			bdbm_error ("invalid request type (%u)", r->req_type);
			bdbm_bug_on (1);
			break;
		}

		if (r->req_type == REQTYPE_RMW_READ)
			r->phyaddr = &r->phyaddr_r;
		r->kpg_flags = ptr_hlm_req->kpg_flags + (i * nr_kp_per_fp);
		r->pptr_kpgs = ptr_hlm_req->pptr_kpgs + (i * nr_kp_per_fp);
		r->ptr_hlm_req = (void*)ptr_hlm_req;
		if (np->page_oob_size > 0) {
			if ((r->ptr_oob = (uint8_t*)bdbm_malloc_atomic 
					(sizeof (uint8_t) * np->page_oob_size)) == NULL) {
				bdbm_error ("bdbm_malloc_atomic failed");
				goto fail;
			}
		} else
			r->ptr_oob = NULL;

		/* keep some metadata in OOB if it is write (e.g., LPA) */
		if ((r->req_type == REQTYPE_WRITE || 
			 r->req_type == REQTYPE_META_WRITE ||
			 r->req_type == REQTYPE_RMW_READ)) {
			if (r->ptr_oob)
				((uint64_t*)r->ptr_oob)[0] = r->lpa;
		}

		/* set elapsed time */
	}

	/* TODO: we assume that 'ptr_llm_inf->make_req' always returns success.
	 * It must be improved to handle the following two cases later
	 * (1) when some of llm_reqs fail
	 * (2) when all of llm_reqs fail */
	for (i = 0; i < hlm_len; i++) {
		if ((ret = bdi->ptr_llm_inf->make_req (bdi, pptr_llm_req[i])) != 0) {
			bdbm_error ("llm_make_req failed");
		}
	}

	/* free pptr_llm_req */
	bdbm_free_atomic (pptr_llm_req);

	return 0;

fail:
	/* free llm_req */
	for (i = 0; i < ptr_hlm_req->len; i++) {
		bdbm_llm_req_t* r = pptr_llm_req[i];
		if (r != NULL) {
			if (r->ptr_oob != NULL)
				bdbm_free_atomic (r->ptr_oob);
			bdbm_free_atomic (r);
		}
	}
	bdbm_free_atomic (pptr_llm_req);

	return 1;
}

uint32_t hlm_nobuf_make_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* ptr_hlm_req)
{
	uint32_t ret, loop = 0;
	bdbm_ftl_params* dp = BDBM_GET_DRIVER_PARAMS (bdi);
	bdbm_stopwatch_t sw;
	bdbm_stopwatch_start (&sw);

	if (dp->mapping_type != MAPPING_POLICY_DFTL) {
		bdbm_ftl_inf_t* ftl = (bdbm_ftl_inf_t*)BDBM_GET_FTL_INF(bdi);
		/* see if foreground GC is needed or not */
		for (loop = 0; loop < 10; loop++) {
			if (ptr_hlm_req->req_type == REQTYPE_WRITE && 
				ftl->is_gc_needed != NULL && 
				ftl->is_gc_needed (bdi)) {
				/* perform GC before sending requests */ 
				ftl->do_gc (bdi);
			} else
				break;
		}
	}
	if (loop > 1) {
		bdbm_msg ("GC invokation: %d", loop);
	}

	switch (ptr_hlm_req->req_type) {
	case REQTYPE_TRIM:
		if ((ret = __hlm_nobuf_make_trim_req (bdi, ptr_hlm_req)) == 0) {
			/* call 'ptr_host_inf->end_req' directly */
			bdi->ptr_host_inf->end_req (bdi, ptr_hlm_req);
			/* ptr_hlm_req is now NULL */
		}
		break;
	case REQTYPE_READ:
	case REQTYPE_WRITE:
		ret = __hlm_nobuf_make_rw_req (bdi, ptr_hlm_req);
		break;
	default:
		bdbm_error ("invalid REQTYPE (%u)", ptr_hlm_req->req_type);
		bdbm_bug_on (1);
		ret = 1;
		break;
	}

#if 0
	if (dp->mapping_type != MAPPING_POLICY_DFTL) {
		if (ptr_hlm_req->req_type == REQTYPE_WRITE) {
			bdbm_msg ("%llu us", 
				bdbm_stopwatch_get_elapsed_time_us (&sw));
		}
	}
#endif

	return ret;
}

void __hlm_nobuf_end_host_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req)
{
	bdbm_hlm_req_t* ptr_hlm_req = (bdbm_hlm_req_t* )ptr_llm_req->ptr_hlm_req;

	/* see if 'ptr_hlm_req' is NULL or not */
	if (ptr_hlm_req == NULL) {
		bdbm_error ("ptr_hlm_req is NULL");
		return;
	}

	/* see if llm's lpa is correct or not */
	if (ptr_hlm_req->lpa > ptr_llm_req->lpa || 
		ptr_hlm_req->lpa + ptr_hlm_req->len <= ptr_llm_req->lpa) {
		bdbm_error ("hlm_req->lpa: %llu-%llu, llm_req->lpa: %llu", 
			ptr_hlm_req->lpa, 
			ptr_hlm_req->lpa + ptr_hlm_req->len, 
			ptr_llm_req->lpa);
		return;
	}

	/* change flags of hlm */
	if (ptr_hlm_req->kpg_flags != NULL) {
		bdbm_device_params_t* np;
		uint32_t nr_kp_per_fp, ofs, loop;

		np = BDBM_GET_DEVICE_PARAMS(bdi);
		nr_kp_per_fp = np->page_main_size / KERNEL_PAGE_SIZE;	/* e.g., 2 = 8 KB / 4 KB */
		ofs = (ptr_llm_req->lpa - ptr_hlm_req->lpa) * nr_kp_per_fp;

		for (loop = 0; loop < nr_kp_per_fp; loop++) {
			/* change the status of kernel pages */
			if (ptr_hlm_req->kpg_flags[ofs+loop] != MEMFLAG_FRAG_PAGE &&
				ptr_hlm_req->kpg_flags[ofs+loop] != MEMFLAG_KMAP_PAGE &&
				ptr_hlm_req->kpg_flags[ofs+loop] != MEMFLAG_FRAG_PAGE_DONE &&
				ptr_hlm_req->kpg_flags[ofs+loop] != MEMFLAG_KMAP_PAGE_DONE) {
				bdbm_error ("kpg_flags is not valid");
				/*bdbm_spin_unlock (&ptr_hlm_req->lock);*/
				/*return;*/
			}
			/*bdbm_spin_lock (&ptr_hlm_req->lock);*/
			ptr_hlm_req->kpg_flags[ofs+loop] |= MEMFLAG_DONE;
			/*bdbm_spin_unlock (&ptr_hlm_req->lock);*/
		}
	}

	/* free oob space & ptr_llm_req */
	if (ptr_llm_req->ptr_oob != NULL) {
		/* LPA stored on OOB must be the same as  */
#if 0
		if (ptr_llm_req->req_type == REQTYPE_READ) {
			uint64_t lpa = ((uint64_t*)ptr_llm_req->ptr_oob)[0];
			if (lpa != ptr_llm_req->lpa) {
				bdbm_warning ("%llu != %llu (%llX)", ptr_llm_req->lpa, lpa, lpa);
			}	
		}
#endif
		bdbm_free_atomic (ptr_llm_req->ptr_oob);
	}
	bdbm_free_atomic (ptr_llm_req);

	/* increase # of reqs finished */
	/*bdbm_spin_lock (&ptr_hlm_req->lock);*/
	ptr_hlm_req->nr_done_reqs++; 
	/*bdbm_spin_unlock (&ptr_hlm_req->lock);*/
	if (ptr_hlm_req->nr_done_reqs == ptr_hlm_req->len) {
		/* finish the host request */
		bdi->ptr_host_inf->end_req (bdi, ptr_hlm_req);
	}
}

void __hlm_nobuf_end_gc_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* llm_req)
{
	bdbm_hlm_req_gc_t* hlm_req = (bdbm_hlm_req_gc_t* )llm_req->ptr_hlm_req;

	hlm_req->nr_done_reqs++;
	if (hlm_req->nr_reqs == hlm_req->nr_done_reqs) {
		bdbm_mutex_unlock (&hlm_req->gc_done);
	}
}

void hlm_nobuf_end_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* llm_req)
{
	switch (llm_req->req_type) {
	case REQTYPE_READ:
	case REQTYPE_READ_DUMMY:
	case REQTYPE_WRITE:
	case REQTYPE_RMW_READ:
	case REQTYPE_RMW_WRITE:
	case REQTYPE_META_WRITE:
	case REQTYPE_META_READ:
		__hlm_nobuf_end_host_req (bdi, llm_req);
		break;
	case REQTYPE_GC_ERASE:
	case REQTYPE_GC_READ:
	case REQTYPE_GC_WRITE:
		__hlm_nobuf_end_gc_req (bdi, llm_req);
		break;
	case REQTYPE_TRIM:
	default:
		bdbm_error ("hlm_nobuf_end_req got an invalid llm_req");
		bdbm_bug_on (1);
		break;
	}
}

