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

#include "debug.h"
#include "dm_dumbssd.h"
#include "dev_hynix_nvme.h"

#define BITS_PER_SLICE 	6
#define BITS_PER_WU 	7
#define BITS_PER_DIE	6

#define USE_ASYNC

static void submit_io_done (struct request *rq, int error)
{
	hd_req_t* hc = rq->end_io_data;
	void (*done)(void*) = hc->done;

	if (hc->rw == READ && hc->kp_ptr)
		memcpy (hc->kp_ptr, hc->buffer, 4096);

	if (rq->cmd)
		kfree (rq->cmd);
	if (hc->buffer)
		kfree (hc->buffer);
	if (rq)
		blk_mq_free_request (rq);
	if (done && hc->req == NULL) /* for user I-O */
		done (hc);
	if (done && hc->req) { /* for block I-O */
		done (hc->req);
		kfree (hc);
	}
}

int simple_read (dumb_ssd_dev_t* dev, hd_req_t* hc)
{
	struct request *rq;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	int req_ofs = hc->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hc->die << (BITS_PER_WU + BITS_PER_SLICE) |
				  hc->wu << (BITS_PER_SLICE);

	bdbm_bug_on (cmd == NULL);

	/* [STEP2] alloc request */
	rq = blk_mq_alloc_request(dev->q, 0, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hc;

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

	if (blk_rq_map_kern (dev->q, rq, hc->buffer, 64*4096, GFP_KERNEL)) {
		bdbm_msg ("blk_rq_map_kern() failed");
		bdbm_bug_on (1);
	}

#ifdef USE_ASYNC
	blk_execute_rq_nowait (dev->q, NULL, rq, 0, submit_io_done);
#else
	blk_execute_rq (dev->q, dev->gd, rq, 0);
	rq->end_io_data = hc;
	submit_io_done (rq, 0);
#endif

	return 0;
}

int simple_write (dumb_ssd_dev_t* dev, hd_req_t* hc)
{
	struct request *rq;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	int req_ofs = hc->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hc->die << (BITS_PER_WU + BITS_PER_SLICE) |
				  hc->wu << (BITS_PER_SLICE);

	bdbm_bug_on (cmd == NULL);

	/* setup bio */
	if (hc->kp_ptr)
		memcpy (hc->buffer, hc->kp_ptr, 4096);

	/* allocate request */
	rq = blk_mq_alloc_request (dev->q, 1, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd_type = REQ_TYPE_DRV_PRIV;
	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hc;

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

	if (blk_rq_map_kern (dev->q, rq, hc->buffer, 64*4096, GFP_KERNEL)) {
		bdbm_msg ("blk_rq_map_kern() failed");
		bdbm_bug_on (1);
	}

#ifdef USE_ASYNC
	blk_execute_rq_nowait (dev->q, NULL, rq, 0, submit_io_done);
#else
	blk_execute_rq (dev->q, dev->gd, rq, 0);
	rq->end_io_data = hc;
	submit_io_done (rq, 0);
#endif

	return 0;
}

int simple_erase (dumb_ssd_dev_t* dev, hd_req_t* hc)
{
	struct request *rq;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	__u32 req_ofs = hc->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
					hc->die << (BITS_PER_WU + BITS_PER_SLICE);
	__le64* ubuffer_64 = (__le64*)hc->buffer;

	bdbm_bug_on (cmd == NULL);

	ubuffer_64[1] = req_ofs;

	/* alloc request */
	rq = blk_mq_alloc_request(dev->q, 1, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hc;

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

	/*if (blk_rq_map_kern (dev->q, rq, hc->buffer, 64*4096, GFP_KERNEL)) {*/
	if (blk_rq_map_kern (dev->q, rq, hc->buffer, 64*4096, GFP_KERNEL)) {
		bdbm_msg ("blk_rq_map_kern() failed");
		bdbm_bug_on (1);
	}

#ifdef USE_ASYNC
	blk_execute_rq_nowait (dev->q, NULL, rq, 0, submit_io_done);
#else 
	blk_execute_rq (dev->q, dev->gd, rq, 0);
	rq->end_io_data = hc;
	submit_io_done (rq, 0);
#endif

	return 0;
}

uint32_t dev_hynix_nvme_submit_io (
	dumb_ssd_dev_t* dev, 
	bdbm_llm_req_t* r, 
	void (*done)(void*))
{
	uint32_t ret = -1;
	hd_req_t* hc = kzalloc (sizeof (hd_req_t), GFP_KERNEL);

	bdbm_bug_on (hc == NULL);

	hc->dev = dev;
	hc->req = r;
	hc->die = r->phyaddr.channel_no;
	hc->block = r->phyaddr.block_no;
	hc->wu = r->phyaddr.page_no;
	hc->done = done;
	hc->buffer = kmalloc (4096*64, GFP_KERNEL);
	if (r)
		hc->kp_ptr = r->fmain.kp_ptr[0];
	else
		hc->kp_ptr = NULL;

	bdbm_bug_on (hc->buffer == NULL);

	switch (r->req_type) {
	case REQTYPE_READ_DUMMY:
		kfree (hc->buffer);
		kfree (hc);
		done (r);
		break;

	case REQTYPE_WRITE:
	case REQTYPE_GC_WRITE:
	case REQTYPE_RMW_WRITE:
	case REQTYPE_META_WRITE:
		hc->rw = WRITE;
		ret = simple_write (dev, hc);
		break;

	case REQTYPE_READ:
	case REQTYPE_GC_READ:
	case REQTYPE_RMW_READ:
	case REQTYPE_META_READ:
		hc->rw = READ;
		ret = simple_read (dev, hc);
		break;

	case REQTYPE_GC_ERASE:
		hc->rw = 0xFF;
		ret = simple_erase (dev, hc);
		break;

	default:
		bdbm_error ("invalid REQTYPE (%u)", r->req_type);
		bdbm_bug_on (1);
		break;
	}

	return 0;
}

