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
#include <linux/blkdev.h>

#include "bdbm_drv.h"
#include "debug.h"
#include "platform.h"
#include "host_block.h"
#include "params.h"
#include "ioctl.h"

#include "utils/utime.h"

/*#define ENABLE_DISPLAY*/

/* interface for host */
struct bdbm_host_inf_t _host_block_inf = {
	.ptr_private = NULL,
	.open = host_block_open,
	.close = host_block_close,
	.make_req = host_block_make_req,
	.end_req = host_block_end_req,
};

static struct bdbm_device_t {
	struct gendisk *gd;
	struct request_queue *queue;
#ifdef USE_COMPLETION
	bdbm_completion make_request_lock;
#else
	bdbm_mutex make_request_lock;
#endif
} bdbm_device;

static uint32_t bdbm_device_major_num = 0;
static struct block_device_operations bdops = {
	.owner = THIS_MODULE,
	.ioctl = bdbm_blk_ioctl,
	//.getgeo = bdbm_blk_getgeo,
};

/* global data structure */
extern struct bdbm_drv_info* _bdi;


static struct bdbm_hlm_req_t* __host_block_create_hlm_trim_req (
	struct bdbm_drv_info* bdi, 
	struct bio* bio)
{
	struct bdbm_hlm_req_t* hlm_req = NULL;
	struct nand_params* np = &bdi->ptr_bdbm_params->nand;
	struct driver_params* dp = &bdi->ptr_bdbm_params->driver;
	uint64_t nr_secs_per_fp = 0;

	nr_secs_per_fp = np->page_main_size / KERNEL_SECTOR_SIZE;

	/* create bdbm_hm_req_t */
	if ((hlm_req = (struct bdbm_hlm_req_t*)bdbm_malloc_atomic
			(sizeof (struct bdbm_hlm_req_t))) == NULL) {
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

static struct bdbm_hlm_req_t* __host_block_create_hlm_rq_req (
	struct bdbm_drv_info* bdi, 
	struct bio* bio)
{
	struct bio_vec *bvec = NULL;
	struct bdbm_hlm_req_t* hlm_req = NULL;
	struct nand_params* np = &bdi->ptr_bdbm_params->nand;

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
	if ((hlm_req = (struct bdbm_hlm_req_t*)bdbm_malloc_atomic
			(sizeof (struct bdbm_hlm_req_t))) == NULL) {
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
			(sizeof(uint8_t*) * hlm_req->len * nr_kp_per_fp)) == NULL) { bdbm_error ("bdbm_malloc_atomic failed"); goto fail_req;
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

static struct bdbm_hlm_req_t* __host_block_create_hlm_req (
	struct bdbm_drv_info* bdi, 
	struct bio* bio)
{
	struct bdbm_hlm_req_t* hlm_req = NULL;
	struct nand_params* np = &bdi->ptr_bdbm_params->nand;
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
		hlm_req = __host_block_create_hlm_trim_req (bdi, bio);
	} else {
		/* make a high-level request for READ or WRITE */
		hlm_req = __host_block_create_hlm_rq_req (bdi, bio);
	}

	/* start a stopwatch */
	if (hlm_req) {
		bdbm_stopwatch_start (&hlm_req->sw);
	}

	return hlm_req;
}

static void __host_block_delete_hlm_req (
	struct bdbm_drv_info* bdi, 
	struct bdbm_hlm_req_t* hlm_req)
{
	struct nand_params* np = NULL;
	uint32_t kpg_loop = 0;
	uint32_t nr_kp_per_fp = 0;

	np = &bdi->ptr_bdbm_params->nand;
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

static void __host_block_display_req (
	struct bdbm_drv_info* bdi, 
	struct bdbm_hlm_req_t* hlm_req)
{
#ifdef ENABLE_DISPLAY
	struct bdbm_ftl_inf_t* ftl = (struct bdbm_ftl_inf_t*)BDBM_GET_FTL_INF(bdi);
	uint64_t seg_no = 0;

	if (ftl->get_segno) {
		seg_no = ftl->get_segno (bdi, hlm_req->lpa);
	}

	switch (hlm_req->req_type) {
	case REQTYPE_TRIM:
		bdbm_msg ("[%llu] TRIM\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);
		break;
	case REQTYPE_READ:
		/*bdbm_msg ("[%llu] READ\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);*/
		break;
	case REQTYPE_WRITE:
		/*bdbm_msg ("[%llu] WRITE\t%llu\t%llu", seg_no, hlm_req->lpa, hlm_req->len);*/
		break;
	default:
		bdbm_error ("invalid REQTYPE (%u)", hlm_req->req_type);
		break;
	}
#endif
}

static void __host_block_make_request (
	struct request_queue *q, 
	struct bio *bio)
{
	/* see if q or bio is valid or not */
	if (q == NULL || bio == NULL) {
		bdbm_msg ("q or bio is NULL; ignore incoming requests");
		return;
	}

	/* grab the lock until a host request is sent to hlm */
#ifdef USE_COMPLETION
	bdbm_wait_for_completion (bdbm_device.make_request_lock);
	bdbm_reinit_completion (bdbm_device.make_request_lock);
#else
	bdbm_mutex_lock (&bdbm_device.make_request_lock);
#endif

	host_block_make_req (_bdi, (void*)bio);

	/* free the lock*/
#ifdef USE_COMPLETION
	bdbm_complete (bdbm_device.make_request_lock);          
#else
	bdbm_mutex_unlock (&bdbm_device.make_request_lock);          
#endif
}

static uint32_t __host_block_register_block_device (struct bdbm_drv_info* bdi)
{
	struct bdbm_params* p = bdi->ptr_bdbm_params;

	/* create a completion lock */
#ifdef USE_COMPLETION
	bdbm_init_completion (bdbm_device.make_request_lock);
	bdbm_complete (bdbm_device.make_request_lock);
#else
	bdbm_mutex_init (&bdbm_device.make_request_lock);
#endif

	/* create a blk queue */
	if (!(bdbm_device.queue = blk_alloc_queue (GFP_KERNEL))) {
		bdbm_error ("blk_alloc_queue failed");
		return -ENOMEM;
	}
	blk_queue_make_request (bdbm_device.queue, __host_block_make_request);
	blk_queue_logical_block_size (bdbm_device.queue, p->driver.kernel_sector_size);
	blk_queue_io_min (bdbm_device.queue, p->nand.page_main_size);
	blk_queue_io_opt (bdbm_device.queue, p->nand.page_main_size);

	/*blk_limits_max_hw_sectors (&bdbm_device.queue->limits, 16);*/

	/* see if a TRIM command is used or not */
	if (p->driver.trim == TRIM_ENABLE) {
		bdbm_device.queue->limits.discard_granularity = KERNEL_PAGE_SIZE;
		bdbm_device.queue->limits.max_discard_sectors = UINT_MAX;
		/*bdbm_device.queue->limits.discard_zeroes_data = 1;*/
		queue_flag_set_unlocked (QUEUE_FLAG_DISCARD, bdbm_device.queue);
		bdbm_msg ("TRIM is enabled");
	} else {
		bdbm_msg ("TRIM is disabled");
	}

	/* register a blk device */
	if ((bdbm_device_major_num = register_blkdev (bdbm_device_major_num, "blueDBM")) < 0) {
		bdbm_msg ("register_blkdev failed (%d)", bdbm_device_major_num);
		return bdbm_device_major_num;
	}
	if (!(bdbm_device.gd = alloc_disk (1))) {
		bdbm_msg ("alloc_disk failed");
		unregister_blkdev (bdbm_device_major_num, "blueDBM");
		return -ENOMEM;
	}
	bdbm_device.gd->major = bdbm_device_major_num;
	bdbm_device.gd->first_minor = 0;
	bdbm_device.gd->fops = &bdops;
	bdbm_device.gd->queue = bdbm_device.queue;
	bdbm_device.gd->private_data = NULL;
	strcpy (bdbm_device.gd->disk_name, "blueDBM");
	set_capacity (bdbm_device.gd, p->nand.device_capacity_in_byte / KERNEL_SECTOR_SIZE);
	add_disk (bdbm_device.gd);

	return 0;
}

void __host_block_unregister_block_device (struct bdbm_drv_info* bdi)
{
	/* unregister a BlueDBM device driver */
	del_gendisk (bdbm_device.gd);
	put_disk (bdbm_device.gd);
	unregister_blkdev (bdbm_device_major_num, "blueDBM");
	blk_cleanup_queue (bdbm_device.queue);
}

uint32_t host_block_open (struct bdbm_drv_info* bdi)
{
	uint32_t ret;
	struct bdbm_host_block_private* p;

	/* create a private data structure */
	if ((p = (struct bdbm_host_block_private*)bdbm_malloc_atomic
			(sizeof (struct bdbm_host_block_private))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return 1;
	}
	p->nr_host_reqs = 0;
	bdbm_spin_lock_init (&p->lock); 
	bdi->ptr_host_inf->ptr_private = (void*)p;

	/* register blueDBM */
	if ((ret = __host_block_register_block_device (bdi)) != 0) {
		bdbm_error ("failed to register blueDBM");
		bdbm_free_atomic (p);
		return 1;
	}

	return 0;
}

void host_block_close (struct bdbm_drv_info* bdi)
{
	unsigned long flags;
	struct bdbm_host_block_private* p = NULL;

	p = (struct bdbm_host_block_private*)BDBM_HOST_PRIV(bdi);

	/* wait for host reqs to finish */
	bdbm_msg ("wait for host reqs to finish");
	for (;;) {
		bdbm_spin_lock_irqsave (&p->lock, flags);
		if (p->nr_host_reqs == 0) {
			bdbm_spin_unlock_irqrestore (&p->lock, flags);
			break;
		}
		bdbm_spin_unlock_irqrestore (&p->lock, flags);
		schedule (); /* sleep */
	}

	/* unregister a block device */
	__host_block_unregister_block_device (bdi);

	/* free private */
	bdbm_free_atomic (p);
}

void host_block_make_req (
	struct bdbm_drv_info* bdi, 
	void* req)
{
	unsigned long flags;
	struct nand_params* np = NULL;
	struct bdbm_hlm_req_t* hlm_req = NULL;
	struct bdbm_host_block_private* p = NULL;
	struct bio* bio = (struct bio*)req;

	np = &bdi->ptr_bdbm_params->nand;
	p = (struct bdbm_host_block_private*)BDBM_HOST_PRIV(bdi);

	/* see if the address range of bio is beyond storage space */
	if (bio->bi_sector + bio_sectors (bio) > np->device_capacity_in_byte / KERNEL_SECTOR_SIZE) {
		bdbm_error ("bio is beyond storage space (%lu > %llu)",
			bio->bi_sector + bio_sectors (bio),
			np->device_capacity_in_byte / KERNEL_SECTOR_SIZE);
		bio_io_error (bio);
		return;
	}

	/* create a hlm_req using a bio */
	if ((hlm_req = __host_block_create_hlm_req (bdi, bio)) == NULL) {
		bdbm_error ("the creation of hlm_req failed");
		bio_io_error (bio);
		return;
	}

	/* display req info */
	__host_block_display_req (bdi, hlm_req);

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
		bio_io_error (bio);
	}
}

void host_block_end_req (
	struct bdbm_drv_info* bdi, 
	struct bdbm_hlm_req_t* hlm_req)
{
	uint32_t ret;
	unsigned long flags;
	struct bio* bio = NULL;
	struct bdbm_host_block_private* p = NULL;


	/* get a bio from hlm_req */
	bio = (struct bio*)hlm_req->ptr_host_req;
	p = (struct bdbm_host_block_private*)BDBM_HOST_PRIV(bdi);
	ret = hlm_req->ret;

	/* destroy hlm_req */
	__host_block_delete_hlm_req (bdi, hlm_req);

	/* get the result and end a bio */
	if (bio != NULL) {
		if (ret == 0)
			bio_endio (bio, 0);
		else
			bio_io_error (bio);
	}

	/* decreate # of reqs */
	bdbm_spin_lock_irqsave (&p->lock, flags);
	if (p->nr_host_reqs > 0)
		p->nr_host_reqs--;
	else
		bdbm_bug_on (1);
	bdbm_spin_unlock_irqrestore (&p->lock, flags);
}

