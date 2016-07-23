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

#if defined (KERNEL_MODE)
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <linux/workqueue.h> /* workqueue */

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include <linux/nvme.h>
#include <linux/blk-mq.h>
#include "hynix_dumbssd.h"


#define BITS_PER_SLICE 	6
#define BITS_PER_WU 	7
#define BITS_PER_DIE	6

/*#define USE_ASYNC*/


struct hd_req {
	bdbm_drv_info_t* bdi;
	bdbm_llm_req_t* r;
	uint64_t die;
	uint64_t block;
	uint64_t wu;
	uint8_t* kp_ptr;
	void* ubuffer;
	void (*intr_handler)(void*);
};

static struct bio* hynix_add_bio_to_req (
	struct request_queue *q,
	int rw,
	uint32_t addr,
	uint8_t* buffer)
{
	struct bio *bio = NULL;
	struct page *page = NULL;

	page = alloc_pages (GFP_NOFS | __GFP_ZERO, 6);
	if (IS_ERR (page)) {
		bdbm_error ("alloc_page() failed");
		return NULL;
	}
	lock_page (page);

	bio = bio_alloc(GFP_NOIO, 64);
	if (!bio) {
		bdbm_error ("bio_alloc() failed");
		return NULL;
	}

	bio->bi_iter.bi_sector = addr * 8;
	bio->bi_rw = rw; /* READ or WRITE */
	bio->bi_private = page;
	bio->bi_end_io = NULL;

	if (rw == WRITE)
		memcpy ((uint8_t*)page_address (page), buffer, 4096);

	bio_add_pc_page(q, bio, page, 4096, 0);

	return bio;
}

static void hynix_del_bio_from_req (struct bio* bio, uint8_t* buffer)
{
	struct page* page = bio->bi_private;

	if (page) {
		if (buffer) 
			memcpy ((uint8_t*)buffer, (uint8_t*)page_address (page), 4096);

		ClearPageUptodate (page);
		unlock_page (page);
		__free_pages (page, 6);
	}

	bio_reset (bio);
	bio_put (bio);
}

static void nvme_nvm_end_io (struct request *rq, int error)
{
	struct hd_req* hdr = rq->end_io_data;

	bdbm_bug_on (hdr == NULL);
	bdbm_bug_on (hdr->r == NULL);
	hdr->intr_handler (hdr->r);

	bdbm_bug_on (rq == NULL);
	bdbm_bug_on (rq->cmd == NULL);
	kfree (rq->cmd);
	if (hdr->ubuffer)
		kfree (hdr->ubuffer);
	kfree (hdr);
	blk_mq_free_request(rq);
}

struct request *nvme_alloc_request(struct request_queue *q,
		struct nvme_command *cmd, unsigned int flags)
{
	bool write = cmd->common.opcode & 1;
	struct request *req;

	req = blk_mq_alloc_request(q, write, flags);
	if (IS_ERR(req))
		return req;

	req->cmd_type = REQ_TYPE_DRV_PRIV;
	req->__data_len = 0;
	req->__sector = (sector_t) -1;
	req->bio = req->biotail = NULL;

	req->cmd = (unsigned char *)cmd;
	req->cmd_len = sizeof(struct nvme_command);
	req->special = (void *)0;

	return req;
}

int simple_read (
	bdbm_drv_info_t* bdi, 
	struct hd_req* hdr)
{
	struct bio* bio = NULL;
	struct request *rq;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	int req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->wu << (BITS_PER_SLICE);

	bdbm_msg ("READ: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);

	/* setup bio */
	bio = hynix_add_bio_to_req (bdi->q, READ, req_ofs, NULL);


	/* alloc request */
	rq = blk_mq_alloc_request(bdi->q, 0, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}
	rq->cmd_type = REQ_TYPE_DRV_PRIV;
	rq->ioprio = bio_prio(bio);

	if (bio_has_data(bio))
		rq->nr_phys_segments = bio_phys_segments(bdi->q, bio);
	rq->__data_len = bio->bi_iter.bi_size;
	rq->bio = rq->biotail = bio;

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hdr;

	/* setup cmd */
	cmd->rw.opcode = 0x02; /* 0x02: READ, 0x01: WRITE */
	cmd->rw.flags = 0;
	cmd->rw.nsid = 1;
	cmd->rw.slba = req_ofs; /* it must be the unit of 255 */
	cmd->rw.length = 63; /* it must be the unit of 255 */
	cmd->rw.control = 0;
	cmd->rw.dsmgmt = 0;
	cmd->rw.reftag = 0;
	cmd->rw.apptag = 0;
	cmd->rw.appmask = 0;

#ifdef USE_ASYNC
	blk_execute_rq_nowait (bdi->q, bdi->gd, rq, 0, nvme_nvm_end_io);
#else
	blk_execute_rq (bdi->q, bdi->gd, rq, 0);
	hynix_del_bio_from_req (bio, hdr->kp_ptr);
	rq->end_io_data = hdr;
	nvme_nvm_end_io (rq, 0);
#endif

	return 0;
}

int simple_write (
	bdbm_drv_info_t* bdi, 
	struct hd_req* hdr)
{
	struct bio* bio = NULL;
	struct request *rq;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	int req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->wu << (BITS_PER_SLICE);

	bdbm_msg ("WRITE: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);

	/* setup bio */
	bio = hynix_add_bio_to_req (bdi->q, WRITE, req_ofs, hdr->kp_ptr);

	/* allocate request */
	rq = blk_mq_alloc_request (bdi->q, 1, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd_type = REQ_TYPE_DRV_PRIV;
	rq->ioprio = bio_prio(bio);
	if (bio_has_data(bio))
		rq->nr_phys_segments = bio_phys_segments(bdi->q, bio);
	rq->__data_len = bio->bi_iter.bi_size;
	rq->bio = rq->biotail = bio;

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hdr;

	/* setup cmd */
	cmd->rw.opcode = 0x01; /* 0x02: READ, 0x01: WRITE */
	cmd->rw.flags = 0;
	cmd->rw.nsid = 1;
	cmd->rw.slba = req_ofs; /* it must be the unit of 255 */
	cmd->rw.length = 63; /* it must be the unit of 255 */
	cmd->rw.control = 0;
	cmd->rw.dsmgmt = 0;
	cmd->rw.reftag = 0;
	cmd->rw.apptag = 0;
	cmd->rw.appmask = 0;

#ifdef USE_ASYNC
	blk_execute_rq_nowait (bdi->q, bdi->gd, rq, 0, nvme_nvm_end_io);
#else
	blk_execute_rq (bdi->q, bdi->gd, rq, 0);
	hynix_del_bio_from_req (bio, NULL);
	rq->end_io_data = hdr;
	nvme_nvm_end_io (rq, 0);
#endif

	return 0;
}

int simple_erase (
	bdbm_drv_info_t* bdi, 
	struct hd_req* hdr)
{
	struct request *rq;
	struct bio* bio = NULL;
	unsigned bufflen = 64 * 4096;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	void* ubuffer = kzalloc (bufflen, GFP_KERNEL);
	__u32 req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE);
	__le64* ubuffer_64 = (__le64*)ubuffer;
	
	bdbm_msg ("ERASE: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);

	hdr->ubuffer = ubuffer;
	ubuffer_64[1] = req_ofs;

	/* setup bio */
	bio = hynix_add_bio_to_req (bdi->q, WRITE, req_ofs, ubuffer);

	/* alloc request */
	rq = blk_mq_alloc_request(bdi->q, 1, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd_type = REQ_TYPE_DRV_PRIV;
	rq->ioprio = bio_prio(bio);
	if (bio_has_data(bio))
		rq->nr_phys_segments = bio_phys_segments(bdi->q, bio);
	rq->__data_len = bio->bi_iter.bi_size;
	rq->bio = rq->biotail = bio;

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hdr;

	/* setup cmd */
	cmd->common.opcode = 9;
	cmd->common.flags = 0;
	cmd->common.nsid = 1;
	cmd->common.cdw2[0] = 0;
	cmd->common.cdw2[1] = 0;
	cmd->common.cdw10[0] = 0;
	cmd->common.cdw10[1] = 4;
	cmd->common.cdw10[2] = 0;
	cmd->common.cdw10[3] = 0;
	cmd->common.cdw10[4] = 0;
	cmd->common.cdw10[5] = 0;

#ifdef USE_ASYNC
	blk_execute_rq_nowait (bdi->q, bdi->gd, rq, 0, nvme_nvm_end_io);
#else 
	blk_execute_rq (bdi->q, bdi->gd, rq, 0);
	hynix_del_bio_from_req (bio, NULL);
	rq->end_io_data = hdr;
	nvme_nvm_end_io (rq, 0);
#endif

	return 0;
}

uint32_t hynix_dumbssd_send_cmd (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r,
	void (*intr_handler)(void*))
{
	uint32_t ret = -1;
	struct hd_req* hdr = kzalloc (sizeof (struct hd_req), GFP_KERNEL);

	hdr->bdi = bdi;
	hdr->r = r;
	hdr->die = r->phyaddr.channel_no;
	hdr->block = r->phyaddr.block_no;
	hdr->wu = r->phyaddr.page_no;
	hdr->intr_handler = intr_handler;
	hdr->ubuffer = NULL;
	hdr->kp_ptr = NULL;

	switch (r->req_type) {
	case REQTYPE_READ_DUMMY:
		intr_handler (r);
		break;

	case REQTYPE_WRITE:
	case REQTYPE_GC_WRITE:
	case REQTYPE_RMW_WRITE:
	case REQTYPE_META_WRITE:
		hdr->kp_ptr = r->fmain.kp_ptr[0];
		ret = simple_write (bdi, hdr);
		break;

	case REQTYPE_READ:
	case REQTYPE_GC_READ:
	case REQTYPE_RMW_READ:
	case REQTYPE_META_READ:
		hdr->kp_ptr = r->fmain.kp_ptr[0];
		ret = simple_read (bdi, hdr);
		break;

	case REQTYPE_GC_ERASE:
		ret = simple_erase (bdi, hdr);
		break;

	default:
		bdbm_error ("invalid REQTYPE (%u)", r->req_type);
		bdbm_bug_on (1);
		break;
	}

	return 0;
}

