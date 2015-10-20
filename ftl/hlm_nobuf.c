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

uint32_t __hlm_nobuf_make_rw_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* ptr_hlm_req)
{
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS(bdi);
	bdbm_ftl_inf_t* ftl = BDBM_GET_FTL_INF(bdi);
	bdbm_llm_req_t* lr;
	int32_t i;

	bdbm_hlm_for_each_llm_req (lr, ptr_hlm_req, i) {
		/* get a physical location from the FTL */
		if (bdbm_is_read (lr->req_type)) {
			if (ftl->get_ppa (bdi, lr->logaddr.lpa[0], &lr->phyaddr) != 0) {
				lr->req_type = REQTYPE_READ_DUMMY; /* reads for unwritten pages */
			}
		} else if (bdbm_is_write (lr->req_type)) {
			if (ftl->get_free_ppa (bdi, lr->logaddr.lpa[0], &lr->phyaddr) != 0) {
				bdbm_error ("`ftl->get_free_ppa' failed");
				goto fail;
			}
			if (ftl->map_lpa_to_ppa (bdi, lr->logaddr.lpa[0], &lr->phyaddr) != 0) {
				bdbm_error ("`ftl->map_lpa_to_ppa' failed");
				goto fail;
			}
		} else {
			bdbm_error ("oops! invalid type (%llx)", lr->req_type);
			bdbm_bug_on (1);
		}

		/* setup oob */
		((uint64_t*)lr->foob.data)[0] = lr->logaddr.lpa[0];

		/* send llm_req to llm */
		if (bdi->ptr_llm_inf->make_req (bdi, lr) != 0) {
			bdbm_error ("oops! make_req () failed");
			bdbm_bug_on (1);
		}
	}

	bdbm_bug_on (ptr_hlm_req->nr_llm_reqs != i);

	return 0;

fail:
	return 1;
}

uint32_t hlm_nobuf_make_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* ptr_hlm_req)
{
	uint32_t ret, loop = 0;
	bdbm_ftl_params* dp = BDBM_GET_DRIVER_PARAMS (bdi);
	bdbm_stopwatch_t sw;
	bdbm_stopwatch_start (&sw);

	/* is req_type correct? */
	bdbm_bug_on (!bdbm_is_normal (ptr_hlm_req->req_type));

	/* trigger gc if necessary */
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

	/* perform i/o */
	if (bdbm_is_trim (ptr_hlm_req->req_type)) {
		if ((ret = __hlm_nobuf_make_trim_req (bdi, ptr_hlm_req)) == 0) {
			/* call 'ptr_host_inf->end_req' directly */
			bdi->ptr_host_inf->end_req (bdi, ptr_hlm_req);
			/* ptr_hlm_req is now NULL */
		}
	} else {
		ret = __hlm_nobuf_make_rw_req (bdi, ptr_hlm_req);
	} 

	return ret;
}

void __hlm_nobuf_end_blkio_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req)
{
	bdbm_hlm_req_t* hr = (bdbm_hlm_req_t* )ptr_llm_req->ptr_hlm_req;

	/* increase # of reqs finished */
	atomic64_inc (&hr->nr_llm_reqs_done);
	if (atomic64_read (&hr->nr_llm_reqs_done) == hr->nr_llm_reqs) {
		/* finish the host request */
		bdbm_mutex_unlock (&hr->done);
		bdi->ptr_host_inf->end_req (bdi, hr);
	}
}

void __hlm_nobuf_end_gc_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* llm_req)
{
	bdbm_hlm_req_gc_t* hr_gc = (bdbm_hlm_req_gc_t* )llm_req->ptr_hlm_req;

	atomic64_inc (&hr_gc->nr_llm_reqs_done);
	if (atomic64_read (&hr_gc->nr_llm_reqs_done) == hr_gc->nr_llm_reqs) {
		bdbm_mutex_unlock (&hr_gc->done);
	}
}

void hlm_nobuf_end_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* llm_req)
{
	if (bdbm_is_gc (llm_req->req_type)) {
		__hlm_nobuf_end_gc_req (bdi, llm_req);
	} else {
		__hlm_nobuf_end_blkio_req (bdi, llm_req);
	}
}

