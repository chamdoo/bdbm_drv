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
	uint64_t die;
	uint64_t block;
	uint64_t wu;
	uint8_t* kp_ptr;
	void* ubuffer;
	void (*intr_handler)(void*);
};

#include <linux/delay.h>
static void nvme_nvm_end_io (struct request *rq, int error)
{
	struct hd_req* hdr = rq->end_io_data;

	/*bdbm_msg ("hynix -- callback - 1");*/
	/*udelay(1000);*/

	bdbm_bug_on (hdr == NULL);
	bdbm_bug_on (hdr->r == NULL);

	switch (hdr->r->req_type) {
	case REQTYPE_READ:
	case REQTYPE_GC_READ:
	case REQTYPE_RMW_READ:
	case REQTYPE_META_READ:
		bdbm_bug_on (hdr->kp_ptr == NULL);
		bdbm_bug_on (hdr->ubuffer== NULL);
		memcpy (hdr->kp_ptr, (uint8_t*)hdr->ubuffer, 4096);
		break;
	}

	/*bdbm_msg ("hynix -- callback - 2");*/
	/*udelay(1000);*/

	/* call handler */
	hdr->intr_handler (hdr->r);
	/* end */

	/*bdbm_msg ("hynix -- callback - 3");*/
	/*udelay(1000);*/

	bdbm_bug_on (rq == NULL);
	bdbm_bug_on (rq->cmd == NULL);
	kfree (rq->cmd);
	if (hdr->ubuffer)
		kfree (hdr->ubuffer);
	kfree (hdr);
	blk_mq_free_request(rq);

	/*bdbm_msg ("hynix -- callback - 4");*/
	/*udelay(1000);*/
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
	int ret;
	struct request *rq;
	unsigned bufflen = 64 * 4096;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	void* ubuffer = kzalloc (bufflen, GFP_KERNEL);
	int req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->wu << (BITS_PER_SLICE);

	bdbm_msg ("READ: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);

	hdr->ubuffer = ubuffer;

	rq = blk_mq_alloc_request(bdi->q, 0, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	bdbm_bug_on (ubuffer == NULL);
	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret) {
		bdbm_error ("blk_rq_map_kern failed: %x", ret);
		bdbm_bug_on (1);
		/*kfree (cmd);*/
		/*kfree (ubuffer);*/
		/*kfree (hdr);*/
		blk_mq_free_request(rq);
		return -1;
	}

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

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hdr;

#ifdef USE_ASYNC
	blk_execute_rq_nowait (bdi->q, bdi->gd, rq, 0, nvme_nvm_end_io);
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
	int ret;
	struct request *rq;
	unsigned bufflen = 64 * 4096;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	void* ubuffer = kzalloc (bufflen, GFP_KERNEL);

	int req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->wu << (BITS_PER_SLICE);

	hdr->ubuffer = ubuffer;

	bdbm_msg ("WRITE: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);

	memcpy ((uint8_t*)ubuffer, hdr->kp_ptr, 4096);

	rq = blk_mq_alloc_request(bdi->q, 1, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	bdbm_bug_on (ubuffer == NULL);
	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret) {
		bdbm_error ("blk_rq_map_kern failed: %x", ret);
		bdbm_bug_on (1);
		/*kfree (cmd);*/
		/*kfree (ubuffer);*/
		/*kfree (hdr);*/
		blk_mq_free_request(rq);
		return -1;
	}

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

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hdr;

#ifdef USE_ASYNC
	blk_execute_rq_nowait (bdi->q, bdi->gd, rq, 0, nvme_nvm_end_io);
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
	int ret;
	struct request *rq;
	unsigned bufflen = 64 * 4096;
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	void* ubuffer = kzalloc (bufflen, GFP_KERNEL);
	__u32 req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE);
	__le64* ubuffer_64 = (__le64*)ubuffer;
	
	bdbm_msg ("ERASE: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);

	hdr->ubuffer = ubuffer;
	ubuffer_64[1] = req_ofs;

	rq = blk_mq_alloc_request(bdi->q, 1, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	bdbm_bug_on (ubuffer == NULL);
	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret) {
		bdbm_error ("blk_rq_map_kern failed: %x", ret);
		bdbm_bug_on (1);
		/*kfree (hdr);*/
		/*kfree (cmd);*/
		/*kfree (ubuffer);*/
		blk_mq_free_request(rq);
		return -ENOMEM;
	}

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

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = hdr;

#ifdef USE_ASYNC
	blk_execute_rq_nowait (bdi->q, bdi->gd, rq, 0, nvme_nvm_end_io);
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

