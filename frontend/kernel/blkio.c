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

#include <linux/module.h>
#include <linux/blkdev.h> /* bio */

#include "bdbm_drv.h"
#include "debug.h"
#include "platform.h"
#include "params.h"
#include "utime.h"

#include "blkio.h"
#include "blkdev.h"
#include "blkdev_ioctl.h"

/*#define ENABLE_DISPLAY*/

extern bdbm_drv_info_t* _bdi;

bdbm_host_inf_t _host_blockio_inf = {
	.ptr_private = NULL,
	.open = host_blockio_open,
	.close = host_blockio_close,
	.make_req = host_blockio_make_req,
	.end_req = host_blockio_end_req,
};

typedef struct {
	bdbm_mutex_t host_lock;
	atomic64_t nr_reqs;
} bdbm_host_blockio_private_t;



/* This is a call-back function invoked by a block-device layer */
static void __host_blkio_make_request_fn (
	struct request_queue *q, 
	struct bio *bio)
{
	host_blockio_make_req (_bdi, (void*)bio);
}

static bdbm_hlm_req_t* __host_blockio_create_hlm_trim_req (
	bdbm_drv_info_t* bdi, 
	struct bio* bio)
{
	bdbm_hlm_req_t* hlm_req = NULL;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_ftl_params* dp = BDBM_GET_DRIVER_PARAMS (bdi);
	uint64_t nr_secs_per_fp = 0;

	nr_secs_per_fp = np->page_main_size / KERNEL_SECTOR_SIZE;

	/* create bdbm_hm_req_t */
	if ((hlm_req = (bdbm_hlm_req_t*)bdbm_malloc_atomic
			(sizeof (bdbm_hlm_req_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return NULL;
	}

	/* make a high-level request for TRIM */
	hlm_req->req_type = REQTYPE_TRIM;
	
	if (dp->mapping_type == MAPPING_POLICY_SEGMENT) {
		hlm_req->lpa = bio->bi_sector / nr_secs_per_fp;
		hlm_req->len = bio_sectors (bio) / nr_secs_per_fp;
		if (hlm_req->len == 0) 
			hlm_req->len = 1;
	} else {
		hlm_req->lpa = (bio->bi_sector + nr_secs_per_fp - 1) / nr_secs_per_fp;
		if ((hlm_req->lpa * nr_secs_per_fp - bio->bi_sector) > bio_sectors (bio)) {
			bdbm_error ("'hlm_req->lpa (%llu) * nr_secs_per_fp (%llu) - bio->bi_sector (%lu)' (%llu) > bio_sectors (bio) (%u)",
					hlm_req->lpa, nr_secs_per_fp, bio->bi_sector,
					hlm_req->lpa * nr_secs_per_fp - bio->bi_sector,
					bio_sectors (bio));
			hlm_req->len = 0;
		} else {
			hlm_req->len = 
				(bio_sectors (bio) - (hlm_req->lpa * nr_secs_per_fp - bio->bi_sector)) / nr_secs_per_fp;
		}
	}
	hlm_req->nr_done_reqs = 0;
	hlm_req->kpg_flags = NULL;
	hlm_req->pptr_kpgs = NULL;	/* no data */
	hlm_req->ptr_host_req = (void*)bio;
	hlm_req->ret = 0;

	return hlm_req;
}

static bdbm_hlm_req_t* __host_blockio_create_hlm_rq_req (
	bdbm_drv_info_t* bdi, 
	struct bio* bio)
{
	struct bio_vec *bvec = NULL;
	bdbm_hlm_req_t* hlm_req = NULL;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);

	uint32_t loop = 0;
	uint32_t kpg_loop = 0;
	uint32_t bvec_offset = 0;
	uint64_t nr_secs_per_fp = 0;
	uint64_t nr_secs_per_kp = 0;
	uint32_t nr_kp_per_fp = 0;

	/* get # of sectors per flash page */
	nr_secs_per_fp = np->page_main_size / KERNEL_SECTOR_SIZE;
	nr_secs_per_kp = KERNEL_PAGE_SIZE / KERNEL_SECTOR_SIZE;
	nr_kp_per_fp = np->page_main_size / KERNEL_PAGE_SIZE;	/* e.g., 2 = 8 KB / 4 KB */

	/* create bdbm_hm_req_t */
	if ((hlm_req = (bdbm_hlm_req_t*)bdbm_malloc_atomic
			(sizeof (bdbm_hlm_req_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return NULL;
	}

	/* get a bio direction */
	if (bio_data_dir (bio) == READ || bio_data_dir (bio) == READA) {
		hlm_req->req_type = REQTYPE_READ;
	} else if (bio_data_dir (bio) == WRITE) {
		hlm_req->req_type = REQTYPE_WRITE;
	} else {
		bdbm_error ("the direction of a bio is invalid (%lu)", bio_data_dir (bio));
		goto fail_req;
	}

	/* make a high-level request for READ or WRITE */
	hlm_req->lpa = (bio->bi_sector / nr_secs_per_fp);
	hlm_req->len = (bio->bi_sector + bio_sectors (bio) + nr_secs_per_fp - 1) / nr_secs_per_fp - hlm_req->lpa;
	hlm_req->nr_done_reqs = 0;
	hlm_req->ptr_host_req = (void*)bio;
	hlm_req->ret = 0;
	bdbm_spin_lock_init (&hlm_req->lock);
	if ((hlm_req->pptr_kpgs = (uint8_t**)bdbm_malloc_atomic
			(sizeof(uint8_t*) * hlm_req->len * nr_kp_per_fp)) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed"); 
		goto fail_req;
	}
	if ((hlm_req->kpg_flags = (uint8_t*)bdbm_malloc_atomic
			(sizeof(uint8_t) * hlm_req->len * nr_kp_per_fp)) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail_flags;
	}
	/* kpg_flags is set to MEMFLAG_NOT_SET (0) */

	/* get or alloc pages */
	bio_for_each_segment (bvec, bio, loop) {
		/* check some error cases */
		if (bvec->bv_offset != 0) {
			bdbm_warning ("'bv_offset' is not 0 (%d)", bvec->bv_offset);
			/*goto fail_grab_pages;*/
		}

next_kpg:
 		/* assign a new page */
		if ((hlm_req->lpa * nr_kp_per_fp + kpg_loop) != (bio->bi_sector + bvec_offset) / nr_secs_per_kp) {
			hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)bdbm_malloc_atomic (KERNEL_PAGE_SIZE);
			hlm_req->kpg_flags[kpg_loop] = MEMFLAG_FRAG_PAGE;
			kpg_loop++;
			goto next_kpg;
		}
		
		if ((hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)page_address (bvec->bv_page)) != NULL) {
			hlm_req->kpg_flags[kpg_loop] = MEMFLAG_KMAP_PAGE;
		} else {
			bdbm_error ("kmap failed");
			goto fail_grab_pages;
		}

		bvec_offset += nr_secs_per_kp;
		kpg_loop++;
	}

	/* get additional free pages if necessary */
	while (kpg_loop < hlm_req->len * nr_kp_per_fp) {
		hlm_req->pptr_kpgs[kpg_loop] = (uint8_t*)bdbm_malloc_atomic (KERNEL_PAGE_SIZE);
		hlm_req->kpg_flags[kpg_loop] = MEMFLAG_FRAG_PAGE;
		kpg_loop++;
	}

	return hlm_req;

fail_grab_pages:
	/* release grabbed pages */
	for (kpg_loop = 0; kpg_loop < hlm_req->len * nr_kp_per_fp; kpg_loop++) {
		if (hlm_req->kpg_flags[kpg_loop] == MEMFLAG_FRAG_PAGE) {
			bdbm_free_atomic (hlm_req->pptr_kpgs[kpg_loop]);
		} else if (hlm_req->kpg_flags[kpg_loop] == MEMFLAG_KMAP_PAGE) {
		} else if (hlm_req->kpg_flags[kpg_loop] != MEMFLAG_NOT_SET) {
			bdbm_error ("invalid flags (kpg_flags[%u]=%u)", kpg_loop, hlm_req->kpg_flags[kpg_loop]);
		}
	}
	bdbm_free_atomic (hlm_req->kpg_flags);

fail_flags:
	bdbm_free_atomic (hlm_req->pptr_kpgs);

fail_req:
	bdbm_free_atomic (hlm_req);

	return NULL;
}

static bdbm_hlm_req_t* __host_blockio_create_hlm_req (
	bdbm_drv_info_t* bdi, 
	struct bio* bio)
{
	bdbm_hlm_req_t* hlm_req = NULL;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	uint64_t nr_secs_per_kp = 0;

	/* get # of sectors per flash page */
	nr_secs_per_kp = KERNEL_PAGE_SIZE / KERNEL_SECTOR_SIZE;

	/* see if some error cases */
	if (bio->bi_sector % nr_secs_per_kp != 0) {
		bdbm_warning ("kernel pages are not aligned with disk sectors (%lu mod %llu != 0)",
			bio->bi_sector, nr_secs_per_kp);
		/* go ahead */
	}
	if (KERNEL_PAGE_SIZE > np->page_main_size) {
		bdbm_error ("kernel page (%lu) is larger than flash page (%llu)",
			KERNEL_PAGE_SIZE, np->page_main_size);
		return NULL;
	}

	/* create 'hlm_req' */
	if (bio->bi_rw & REQ_DISCARD) {
		/* make a high-level request for TRIM */
		hlm_req = __host_blockio_create_hlm_trim_req (bdi, bio);
	} else {
		/* make a high-level request for READ or WRITE */
		hlm_req = __host_blockio_create_hlm_rq_req (bdi, bio);
	}

	/* start a stopwatch */
	if (hlm_req) {
		bdbm_stopwatch_start (&hlm_req->sw);
	}

	return hlm_req;
}

static void __host_blockio_delete_hlm_req (
	bdbm_drv_info_t* bdi, 
	bdbm_hlm_req_t* hlm_req)
{
	bdbm_device_params_t* np = NULL;
	uint32_t kpg_loop = 0;
	uint32_t nr_kp_per_fp = 0;

	np = BDBM_GET_DEVICE_PARAMS (bdi);
	nr_kp_per_fp = np->page_main_size / KERNEL_PAGE_SIZE;	/* e.g., 2 = 8 KB / 4 KB */

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
				bdbm_free_atomic (hlm_req->pptr_kpgs[kpg_loop]);
			} else if (hlm_req->kpg_flags[kpg_loop] == MEMFLAG_KMAP_PAGE_DONE) {
			} else if (hlm_req->kpg_flags[kpg_loop] != MEMFLAG_NOT_SET) {
				bdbm_error ("invalid flags (kpg_flags[%u]=%u)", kpg_loop, hlm_req->kpg_flags[kpg_loop]);
			}
		}
	}

	/* release other stuff */
	if (hlm_req->kpg_flags != NULL) 
		bdbm_free_atomic (hlm_req->kpg_flags);
	if (hlm_req->pptr_kpgs != NULL) 
		bdbm_free_atomic (hlm_req->pptr_kpgs);
	bdbm_free_atomic (hlm_req);
}

#ifdef ENABLE_DISPLAY
static void __host_blockio_display_req (
	bdbm_drv_info_t* bdi, 
	bdbm_hlm_req_t* hlm_req)
{
	bdbm_ftl_inf_t* ftl = (bdbm_ftl_inf_t*)BDBM_GET_FTL_INF(bdi);
	uint64_t seg_no = 0;

	if (ftl->get_segno) {
		seg_no = ftl->get_segno (bdi, hlm_req->lpa);
	}

	switch (hlm_req->req_type) {
	case REQTYPE_TRIM:
		bdbm_msg ("[%llu] TRIM\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);
		break;
	case REQTYPE_READ:
		bdbm_msg ("[%llu] READ\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);
		break;
	case REQTYPE_WRITE:
		bdbm_msg ("[%llu] WRITE\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);
		break;
	default:
		bdbm_error ("invalid REQTYPE (%u)", hlm_req->req_type);
		break;
	}
}
#endif

uint32_t host_blockio_open (bdbm_drv_info_t* bdi)
{
	uint32_t ret;
	bdbm_host_blockio_private_t* p;

	/* create a private data structure */
	if ((p = (bdbm_host_blockio_private_t*)bdbm_malloc_atomic
			(sizeof (bdbm_host_blockio_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return 1;
	}
	bdbm_mutex_init (&p->host_lock);
	atomic64_set (&p->nr_reqs, 0);
	bdi->ptr_host_inf->ptr_private = (void*)p;

	/* register blueDBM */
	if ((ret = host_blkdev_register_device
			(bdi, __host_blkio_make_request_fn)) != 0) {
		bdbm_error ("failed to register blueDBM");
		bdbm_free_atomic (p);
		return 1;
	}

	return 0;
}

void host_blockio_close (bdbm_drv_info_t* bdi)
{
	bdbm_host_blockio_private_t* p = NULL;

	p = (bdbm_host_blockio_private_t*)BDBM_HOST_PRIV(bdi);

	/* wait for host reqs to finish */
	bdbm_msg ("wait for host reqs to finish");
	while (1) {
		if (atomic64_read (&p->nr_reqs) == 0)
			break;
		schedule (); /* sleep */
	}

	/* unregister a block device */
	host_blkdev_unregister_block_device (bdi);

	/* free private */
	bdbm_free_atomic (p);
}

void host_blockio_make_req (bdbm_drv_info_t* bdi, void* req)
{
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_host_blockio_private_t* p = (bdbm_host_blockio_private_t*)BDBM_HOST_PRIV(bdi);
	bdbm_hlm_req_t* hlm_req = NULL;
	struct bio* bio = (struct bio*)req;

	/* lock a global mutex -- this function must be finished as soon as possible */
	bdbm_mutex_lock (&p->host_lock);

	/* see if the address range of bio is beyond storage space */
	if (bio->bi_sector + bio_sectors (bio) > np->device_capacity_in_byte / KERNEL_SECTOR_SIZE) {
		bdbm_mutex_unlock (&p->host_lock);
		bdbm_error ("bio is beyond storage space (%lu > %llu)",
			bio->bi_sector + bio_sectors (bio),
			np->device_capacity_in_byte / KERNEL_SECTOR_SIZE);
		bio_io_error (bio);
		return;
	}

	/* create a hlm_req using a bio */
	if ((hlm_req = __host_blockio_create_hlm_req (bdi, bio)) == NULL) {
		bdbm_mutex_unlock (&p->host_lock);
		bdbm_error ("the creation of hlm_req failed");
		bio_io_error (bio);
		return;
	}

#ifdef ENABLE_DISPLAY
	/* display req info */
	__host_blockio_display_req (bdi, hlm_req);
#endif

	/* if success, increase # of host reqs before sending the request to hlm */
	atomic64_inc (&p->nr_reqs);

	/* NOTE: it would be possible that 'hlm_req' becomes NULL 
	 * if 'bdi->ptr_hlm_inf->make_req' is success. */
	if (bdi->ptr_hlm_inf->make_req (bdi, hlm_req) != 0) {
		bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");

		/* decreate # of reqs */
		atomic64_dec (&p->nr_reqs);
		if (atomic64_read (&p->nr_reqs) < 0) {
			bdbm_error ("p->nr_reqs is negative (%ld)", atomic64_read (&p->nr_reqs));
		}

		/* finish a bio */
		__host_blockio_delete_hlm_req (bdi, hlm_req);
		bio_io_error (bio);
	}

	bdbm_mutex_unlock (&p->host_lock);
}

void host_blockio_end_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hlm_req)
{
	uint32_t ret;
	struct bio* bio = NULL;
	bdbm_host_blockio_private_t* p = NULL;

	/* unlock hlm_req's lock if it is available */
	if (hlm_req->done)
		bdbm_mutex_unlock (hlm_req->done);

	/* get a bio from hlm_req */
	bio = (struct bio*)hlm_req->ptr_host_req;
	p = (bdbm_host_blockio_private_t*)BDBM_HOST_PRIV(bdi);
	ret = hlm_req->ret;

	/* destroy hlm_req */
	__host_blockio_delete_hlm_req (bdi, hlm_req);

	/* get the result and end a bio */
	if (bio != NULL) {
		if (ret == 0) bio_endio (bio, 0);
		else bio_io_error (bio);
	}

	/* decreate # of reqs */
	atomic64_dec (&p->nr_reqs);
	if (atomic64_read (&p->nr_reqs) < 0) {
		bdbm_error ("p->nr_reqs is negative (%ld)", atomic64_read (&p->nr_reqs));
	}
}
