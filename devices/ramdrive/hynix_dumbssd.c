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

#define USE_ASYNC

struct hd_req {
	bdbm_drv_info_t* bdi;
	bdbm_llm_req_t* r;
	int rw;
	uint64_t die;
	uint64_t block;
	uint64_t wu;
	uint8_t* kp_ptr;
	uint8_t* buffer;
	void (*intr_handler)(void*);
};

void print_bio (struct bio* bio) 
{
	bdbm_msg ("bio->bi_iter.bi_sector = %d", (int)bio->bi_iter.bi_sector);
	bdbm_msg ("bio->bi_iter.bi_size = %d", (int)bio->bi_iter.bi_size);
	bdbm_msg ("bio->bi_rw = %d", (int)bio->bi_rw);
	bdbm_msg ("bio->bi_private = %p", bio->bi_private);
	bdbm_msg ("bio->bi_end_io = %p", bio->bi_end_io);
}

bdbm_sema_t dbg_seq;


static void nvme_nvm_end_io (struct request *rq, int error)
{
	struct hd_req* hdr = rq->end_io_data;
	bdbm_llm_req_t* r = hdr->r;
	void (*fn) (void*);

	static int init = 0, parallel = 0;;
	if (init == 0) {
		bdbm_sema_init (&dbg_seq);
		init = 1;
	}

	bdbm_sema_lock (&dbg_seq);

	if (parallel > 1) 
		bdbm_msg ("what???");
	parallel++;

	fn = hdr->intr_handler;

	/*bio_put (rq->bio);*/

	/*if (rq->bio && rq->bio->bi_end_io)*/
	/*rq->bio->bi_end_io (rq->bio);*/
	/*if (hdr->rw == READ)*/
	/*memcpy (hdr->kp_ptr, hdr->buffer, 4096);*/
	/*if (rq->cmd)*/
	/*kfree (rq->cmd);*/
	/*if (hdr->buffer)*/
	/*kfree (hdr->buffer);*/
	/*if (hdr)*/
	/*kfree (hdr);*/
	if (rq)
		blk_mq_free_request (rq);

	fn (r);

	parallel--;

	bdbm_msg ("done from %llu (%llu %llu %llu %llu)", 
		r->phyaddr.channel_no,
		r->phyaddr.channel_no,
		r->phyaddr.chip_no,
		r->phyaddr.block_no,
		r->phyaddr.page_no);

	bdbm_sema_unlock (&dbg_seq);

	/*bdbm_msg ("nvme_nvm_end_io -- callback - done");*/
}

static void blk_end_sync_rq (struct request* rq, int error)
{
	struct completion* waiting = rq->end_io_data;
	rq->end_io_data = NULL;
	complete (waiting);
}

int simple_read (
	bdbm_drv_info_t* bdi, 
	struct hd_req* hdr)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct bio* bio = NULL;
	struct request *rq;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	int req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->wu << (BITS_PER_SLICE);

	bdbm_bug_on (cmd == NULL);

	/*bdbm_msg ("READ: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);*/

#if 0
	/* [STEP1] setup bio */
	/*bio = bio_map_kern (bdi->q, hdr->buffer, 64*4096, GFP_KERNEL);*/
	bio = bio_copy_kern (bdi->q, hdr->buffer, 64*4096, GFP_NOIO, 1);
	bdbm_bug_on (bio == NULL);
	bio->bi_rw = READ;
	bio_get (bio);
#endif

	/* [STEP2] alloc request */
	rq = blk_mq_alloc_request(bdi->q, 0, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	/*rq->cmd_type = REQ_TYPE_DRV_PRIV;*/
	/*rq->cmd_flags |= REQ_FAILFAST_DRIVER;*/
	/*rq->ioprio = bio_prio(bio);*/
	/*if (bio_has_data(bio)) {*/
	/*rq->nr_phys_segments = bio_phys_segments(bdi->q, bio);*/
	/*}*/
	/*rq->__data_len = bio->bi_iter.bi_size;*/
	/*rq->bio = rq->biotail = bio;*/

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hdr;

	/* [STEP3] setup cmd */
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

	if (blk_rq_map_kern (bdi->q, rq, hdr->buffer, 64*4096, GFP_KERNEL)) {
		bdbm_msg ("blk_rq_map_kern() failed");
		bdbm_bug_on (1);
	}

#ifdef USE_ASYNC
	/*rq->end_io_data = &wait;*/
	blk_execute_rq_nowait (bdi->q, NULL, rq, 1, nvme_nvm_end_io);
	/*blk_execute_rq_nowait (bdi->q, NULL, rq, 1, blk_end_sync_rq);*/
	/*wait_for_completion_io(&wait);*/
	/*rq->end_io_data = hdr;*/
	/*nvme_nvm_end_io (rq, 0);*/
#else
	blk_execute_rq (bdi->q, bdi->gd, rq, 0);
	rq->end_io_data = hdr;
	nvme_nvm_end_io (rq, 0);
#endif

	return 0;
}

int simple_write (
	bdbm_drv_info_t* bdi, 
	struct hd_req* hdr)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct bio* bio = NULL;
	struct request *rq;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	int req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->wu << (BITS_PER_SLICE);

	bdbm_bug_on (cmd == NULL);

	/*bdbm_msg ("WRITE: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);*/

	/* setup bio */
	memcpy (hdr->buffer, hdr->kp_ptr, 4096);
#if 0
	/*bio = bio_map_kern (bdi->q, hdr->buffer, 64*4096, GFP_NOIO);*/
	bio = bio_copy_kern (bdi->q, hdr->buffer, 64*4096, GFP_NOIO, 0);
	bdbm_bug_on (bio == NULL);
	bio->bi_rw = WRITE;
	bio_get (bio);
#endif

	/* allocate request */
	rq = blk_mq_alloc_request (bdi->q, 1, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd_type = REQ_TYPE_DRV_PRIV;
	/*rq->cmd_flags |= REQ_FAILFAST_DRIVER;*/
	/*rq->ioprio = bio_prio(bio);*/
	/*if (bio_has_data(bio))*/
	/*rq->nr_phys_segments = bio_phys_segments(bdi->q, bio);*/
	/*rq->__data_len = bio->bi_iter.bi_size;*/
	/*rq->bio = rq->biotail = bio;*/

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

	if (blk_rq_map_kern (bdi->q, rq, hdr->buffer, 64*4096, GFP_KERNEL)) {
		bdbm_msg ("blk_rq_map_kern() failed");
		bdbm_bug_on (1);
	}

#ifdef USE_ASYNC
	/*rq->end_io_data = &wait;*/
	blk_execute_rq_nowait (bdi->q, NULL, rq, 1, nvme_nvm_end_io);
	/*blk_execute_rq_nowait (bdi->q, NULL, rq, 1, blk_end_sync_rq);*/
	/*wait_for_completion_io(&wait);*/
	/*rq->end_io_data = hdr;*/
	/*nvme_nvm_end_io (rq, 0);*/
#else
	blk_execute_rq (bdi->q, bdi->gd, rq, 0);
	rq->end_io_data = hdr;
	nvme_nvm_end_io (rq, 0);
#endif

	return 0;
}

int simple_erase (
	bdbm_drv_info_t* bdi, 
	struct hd_req* hdr)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct request *rq;
	struct bio* bio = NULL;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	__u32 req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
					hdr->die << (BITS_PER_WU + BITS_PER_SLICE);
	__le64* ubuffer_64 = (__le64*)hdr->buffer;

	bdbm_bug_on (cmd == NULL);
	
	/*bdbm_msg ("ERASE: %llu %llu => %u (%x)", hdr->block, hdr->die, req_ofs, req_ofs);*/

	ubuffer_64[1] = req_ofs;

	/* setup bio */
#if 0
	/*bio = bio_map_kern (bdi->q, hdr->buffer, 64*4096, GFP_KERNEL);*/
	bio = bio_copy_kern (bdi->q, hdr->buffer, 64*4096, GFP_NOIO, 0);
	bdbm_bug_on (bio == NULL);
	bio->bi_rw = WRITE;
	bio_get (bio);
#endif

	/* alloc request */
	rq = blk_mq_alloc_request(bdi->q, 1, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	/*rq->cmd_type = REQ_TYPE_DRV_PRIV;*/
	/*rq->cmd_flags |= REQ_FAILFAST_DRIVER;*/
	/*rq->ioprio = bio_prio(bio);*/
	/*if (bio_has_data(bio))*/
	/*rq->nr_phys_segments = bio_phys_segments(bdi->q, bio);*/
	/*rq->__data_len = bio->bi_iter.bi_size;*/
	/*rq->bio = rq->biotail = bio;*/

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

	/*if (blk_rq_map_kern (bdi->q, rq, hdr->buffer, 64*4096, GFP_KERNEL)) {*/
	if (blk_rq_map_kern (bdi->q, rq, hdr->buffer, 64*4096, GFP_KERNEL)) {
		bdbm_msg ("blk_rq_map_kern() failed");
		bdbm_bug_on (1);
	}

#ifdef USE_ASYNC
	/*rq->end_io_data = &wait;*/
	blk_execute_rq_nowait (bdi->q, NULL, rq, 1, nvme_nvm_end_io);
	/*blk_execute_rq_nowait (bdi->q, NULL, rq, 1, blk_end_sync_rq);*/
	/*wait_for_completion_io(&wait);*/
	/*rq->end_io_data = hdr;*/
	/*nvme_nvm_end_io (rq, 0);*/
#else 
	blk_execute_rq (bdi->q, bdi->gd, rq, 0);
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

	bdbm_bug_on (hdr == NULL);

	hdr->bdi = bdi;
	hdr->r = r;
	hdr->die = r->phyaddr.channel_no;
	hdr->block = r->phyaddr.block_no;
	hdr->wu = r->phyaddr.page_no;
	hdr->intr_handler = intr_handler;
	hdr->buffer = kmalloc (4096*64, GFP_KERNEL);
	hdr->kp_ptr = r->fmain.kp_ptr[0];

	bdbm_bug_on (hdr->buffer == NULL);

	switch (r->req_type) {
	case REQTYPE_READ_DUMMY:
		kfree (hdr);
		intr_handler (r);
		break;

	case REQTYPE_WRITE:
	case REQTYPE_GC_WRITE:
	case REQTYPE_RMW_WRITE:
	case REQTYPE_META_WRITE:
		hdr->rw = WRITE;
		ret = simple_write (bdi, hdr);
		break;

	case REQTYPE_READ:
	case REQTYPE_GC_READ:
	case REQTYPE_RMW_READ:
	case REQTYPE_META_READ:
		hdr->rw = READ;
		ret = simple_read (bdi, hdr);
		break;

	case REQTYPE_GC_ERASE:
		hdr->rw = 0xFF;
		ret = simple_erase (bdi, hdr);
		break;

	default:
		bdbm_error ("invalid REQTYPE (%u)", r->req_type);
		bdbm_bug_on (1);
		break;
	}

	return 0;
}
EXPORT_SYMBOL (hynix_dumbssd_send_cmd);
