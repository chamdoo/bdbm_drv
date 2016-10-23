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

//#include <linux/module.h>
#include <linux/blkdev.h> /* bio */

#include "bdbm_drv.h"
#include "debug.h"
#include "umemory.h"
#include "params.h"
#include "utime.h"
#include "uthread.h"

#include "blkio.h"
#include "blkdev.h"
#include "blkdev_ioctl.h"

#include "hlm_reqs_pool.h"

/*#define ENABLE_DISPLAY*/

extern bdbm_drv_info_t* _bdi;

bdbm_host_inf_t _blkio_inf = {
	.ptr_private = NULL,
	.open = blkio_open,
	.close = blkio_close,
	.make_req = blkio_make_req,
	.end_req = blkio_end_req,
};

typedef struct {
	bdbm_sema_t host_lock;
	atomic_t nr_host_reqs;
	bdbm_hlm_reqs_pool_t* hlm_reqs_pool;
} bdbm_blkio_private_t;


/* This is a call-back function invoked by a block-device layer */
static bdbm_blkio_req_t* __get_blkio_req (struct bio *bio)
{
	struct bio_vec bvec;
	struct bvec_iter iter;
	bdbm_blkio_req_t* br = (bdbm_blkio_req_t*)bdbm_malloc_atomic (sizeof (bdbm_blkio_req_t));

	/* check the pointer */
	if (br == NULL)
		goto fail;

	/* get the type of the bio request */
	if (bio->bi_rw & REQ_DISCARD)
		br->bi_rw = REQTYPE_TRIM;
	else if (bio_data_dir (bio) == READ || bio_data_dir (bio) == READA)
		br->bi_rw = REQTYPE_READ;
	else if (bio_data_dir (bio) == WRITE)
		br->bi_rw = REQTYPE_WRITE;
	else {
		bdbm_error ("oops! invalid request type (bi->bi_rw = %lx)", bio->bi_rw);
		goto fail;
	}

	/* get the offset and the length of the bio */
	br->bi_offset = bio->bi_iter.bi_sector;
	br->bi_size = bio_sectors (bio);
	br->bi_bvec_cnt = 0;
	br->bio = (void*)bio;

	/* get the data from the bio */
	if (br->bi_rw != REQTYPE_TRIM) {
		bio_for_each_segment (bvec, bio, iter) {
			br->bi_bvec_ptr[br->bi_bvec_cnt] = (uint8_t*)page_address (bvec.bv_page);
			br->bi_bvec_cnt++;

			if (br->bi_bvec_cnt >= BDBM_BLKIO_MAX_VECS) {
				/* NOTE: this is an impossible case unless kernel parameters are changed */
				bdbm_error ("oops! # of vectors in bio is larger than %u %llu", 
					BDBM_BLKIO_MAX_VECS, br->bi_bvec_cnt);
				goto fail;
			}
		}
	}

	return br;

fail:
	if (br)
		bdbm_free_atomic (br);
	return NULL;
}

static void __free_blkio_req (bdbm_blkio_req_t* br)
{
	if (br)
		bdbm_free_atomic (br);
}

//static void __host_blkio_make_request_fn (
static blk_qc_t __host_blkio_make_request_fn (
	struct request_queue *q, 
	struct bio *bio)
{
	blkio_make_req (_bdi, (void*)bio);
	return BLK_QC_T_NONE; /* for no polling */
}


uint32_t blkio_open (bdbm_drv_info_t* bdi)
{
	uint32_t ret;
	bdbm_blkio_private_t* p;
	int mapping_unit_size;

	/* create a private data structure */
	if ((p = (bdbm_blkio_private_t*)bdbm_malloc (sizeof (bdbm_blkio_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return 1;
	}
	bdbm_sema_init (&p->host_lock);
	atomic_set (&p->nr_host_reqs, 0);
	bdi->ptr_host_inf->ptr_private = (void*)p;

	/* create hlm_reqs pool */
	if (bdi->parm_dev.nr_subpages_per_page == 1)
		mapping_unit_size = bdi->parm_dev.page_main_size;
	else
		mapping_unit_size = KERNEL_PAGE_SIZE;

	if ((p->hlm_reqs_pool = bdbm_hlm_reqs_pool_create (
			mapping_unit_size,	/* mapping unit */
			bdi->parm_dev.page_main_size	/* io unit */	
			)) == NULL) {
		bdbm_warning ("bdbm_hlm_reqs_pool_create () failed");
		return 1;
	}

	/* register robusta */
	if ((ret = host_blkdev_register_device
			(bdi, __host_blkio_make_request_fn)) != 0) {
		bdbm_error ("failed to register robusta");
		bdbm_free (p);
		return 1;
	}

	return 0;
}

void blkio_close (bdbm_drv_info_t* bdi)
{
	bdbm_blkio_private_t* p = BDBM_HOST_PRIV (bdi); 

	/* wait until requests to finish */
	if (atomic_read (&p->nr_host_reqs) > 0) {
		bdbm_thread_yield ();
	}

	/* close hlm_reqs pool */
	if (p->hlm_reqs_pool) {
		bdbm_hlm_reqs_pool_destroy (p->hlm_reqs_pool);
	}

	/* unregister a block device */
	host_blkdev_unregister_block_device (bdi);

	/* free private */
	bdbm_free (p);
}

void blkio_make_req (bdbm_drv_info_t* bdi, void* bio)
{
	bdbm_blkio_private_t* p = (bdbm_blkio_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_blkio_req_t* br = NULL;
	bdbm_hlm_req_t* hr = NULL;

	/* get blkio */
	if ((br = __get_blkio_req ((struct bio*)bio)) == NULL) {
		bdbm_error ("__get_blkio_req () failed");
		goto fail;
	}

	/* get a free hlm_req from the hlm_reqs_pool */
	if ((hr = bdbm_hlm_reqs_pool_get_item (p->hlm_reqs_pool)) == NULL) {
		bdbm_error ("bdbm_hlm_reqs_pool_get_item () failed");
		goto fail;
	}

	/* build hlm_req with bio */
	if (bdbm_hlm_reqs_pool_build_req (p->hlm_reqs_pool, hr, br) != 0) {
		bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
		goto fail;
	}

	/* lock a global mutex -- this function must be finished as soon as possible */
	bdbm_sema_lock (&p->host_lock);

	/* if success, increase # of host reqs */
	atomic_inc (&p->nr_host_reqs);

	/* NOTE: it would be possible that 'hlm_req' becomes NULL 
	 * if 'bdi->ptr_hlm_inf->make_req' is success. */
	if (bdi->ptr_hlm_inf->make_req (bdi, hr) != 0) {
		/* oops! something wrong */
		bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");

		/* cancel the request */
		atomic_dec (&p->nr_host_reqs);
	}

	/* ulock a global mutex */
	bdbm_sema_unlock (&p->host_lock);

	return;

fail:
	if (hr)
		bdbm_hlm_reqs_pool_free_item (p->hlm_reqs_pool, hr);
	if (br)
		__free_blkio_req (br);
}
void blkio_end_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr)
{
	bdbm_blkio_private_t* p = (bdbm_blkio_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_blkio_req_t* br = (bdbm_blkio_req_t*)hr->blkio_req;

	/* end bio */
	if (hr->ret == 0)
		bio_endio ((struct bio*)br->bio);
	else {
		bdbm_warning ("oops! make_req () failed with %d", hr->ret);
		bio_io_error ((struct bio*)br->bio);
	}

	/* free blkio_req */
	__free_blkio_req (br);

	/* destroy hlm_req */
	bdbm_hlm_reqs_pool_free_item (p->hlm_reqs_pool, hr);

	/* decreate # of reqs */
	atomic_dec (&p->nr_host_reqs);
}

