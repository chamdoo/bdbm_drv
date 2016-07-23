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
	int die, 
	int block, 
	int wu,
	kp_stt_t* kp_stt,
	uint8_t** kp_ptr)
{
	int ret;
	struct request *rq;
	unsigned bufflen = 64 * 4096;
	/*struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);*/
	struct nvme_command cmd;
	void* ubuffer = kzalloc (bufflen, GFP_KERNEL);

	int req_len = 63;
	int req_ofs = block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  die << (BITS_PER_WU + BITS_PER_SLICE) |
				  wu << (BITS_PER_SLICE);

	rq = blk_mq_alloc_request(bdi->q, 0, 0);
	if (IS_ERR(rq))
		return -ENOMEM;

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret) {
		bdbm_error ("blk_rq_map_kern failed");
		kfree (ubuffer);
		blk_mq_free_request(rq);
		return -1;
	}

	cmd.rw.opcode = 0x02; /* 0x02: READ, 0x01: WRITE */
	cmd.rw.flags = 0;
	cmd.rw.nsid = 1;
	cmd.rw.slba = req_ofs; /* it must be the unit of 255 */
	cmd.rw.length = req_len; /* it must be the unit of 255 */
	cmd.rw.control = 0;
	cmd.rw.dsmgmt = 0;
	cmd.rw.reftag = 0;
	cmd.rw.apptag = 0;
	cmd.rw.appmask = 0;

	rq->cmd = (unsigned char *)&cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = NULL;

	blk_execute_rq (bdi->q, bdi->gd, rq, 0);

	ret = rq->errors;
	if (ret != 0) {
		bdbm_msg ("ret = %u", ret);
	}

	/* copy data to bio */
	memcpy (kp_ptr[0], (uint8_t*)ubuffer, 4096);
	/* end */

	kfree (ubuffer);
	blk_mq_free_request(rq);

	return 0;
}

int simple_write (
	bdbm_drv_info_t* bdi, 
	int die, 
	int block, 
	int wu,
	kp_stt_t* kp_stt,
	uint8_t** kp_ptr)
{
	int ret;
	struct request *rq;
	unsigned bufflen = 64 * 4096;
	struct nvme_command cmd;
	void* ubuffer = kzalloc (bufflen, GFP_KERNEL);

	int req_len = 63;
	int req_ofs = block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  die << (BITS_PER_WU + BITS_PER_SLICE) |
				  wu << (BITS_PER_SLICE);

	/* copy data to bio */
	memcpy ((uint8_t*)ubuffer, kp_ptr[0], 4096);
	/* end */

	rq = blk_mq_alloc_request(bdi->q, 1, 0);
	if (IS_ERR(rq))
		return -ENOMEM;

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret) {
		bdbm_error ("blk_rq_map_kern failed");
		kfree (ubuffer);
		blk_mq_free_request(rq);
		return -1;
	}

	cmd.rw.opcode = 0x01; /* 0x02: READ, 0x01: WRITE */
	cmd.rw.flags = 0;
	cmd.rw.nsid = 1;
	cmd.rw.slba = req_ofs; /* it must be the unit of 255 */
	cmd.rw.length = req_len; /* it must be the unit of 255 */
	cmd.rw.control = 0;
	cmd.rw.dsmgmt = 0;
	cmd.rw.reftag = 0;
	cmd.rw.apptag = 0;
	cmd.rw.appmask = 0;

	rq->cmd = (unsigned char *)&cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = NULL;

	blk_execute_rq (bdi->q, bdi->gd, rq, 0);

	ret = rq->errors;
	if (ret != 0) {
		bdbm_msg ("ret = %u", ret);
	}

	kfree (ubuffer);
	blk_mq_free_request(rq);

	return 0;
}

int simple_erase (bdbm_drv_info_t* bdi, int die, int block)
{
	int ret;
	struct request *rq;
	unsigned bufflen = 64 * 4096;
	struct nvme_command cmd;
	void* ubuffer = kzalloc (bufflen, GFP_KERNEL);
	__le64* ubuffer_64 = (__le64*)ubuffer;

	__u32 req_ofs = block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  die << (BITS_PER_WU + BITS_PER_SLICE);

	ubuffer_64[1] = req_ofs;

	rq = blk_mq_alloc_request(bdi->q, 1, 0);
	if (IS_ERR(rq))
		return -ENOMEM;

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret) {
		kfree (ubuffer);
		blk_mq_free_request(rq);
		return -ENOMEM;
	}

	cmd.common.opcode = 9;
	cmd.common.flags = 0;
	cmd.common.nsid = 1;
	cmd.common.cdw2[0] = 0;
	cmd.common.cdw2[1] = 0;
	cmd.common.cdw10[0] = 0;
	cmd.common.cdw10[1] = 4;
	cmd.common.cdw10[2] = 0;
	cmd.common.cdw10[3] = 0;
	cmd.common.cdw10[4] = 0;
	cmd.common.cdw10[5] = 0;

	rq->cmd = (unsigned char *)&cmd;
	rq->cmd_len = sizeof(struct nvme_command);
	rq->special = (void *)0;
	rq->end_io_data = NULL;

	blk_execute_rq (bdi->q, bdi->gd, rq, 0);

	ret = rq->errors;
	if (ret != 0)
		bdbm_msg ("ret = %u", ret);

	return 0;
}

uint32_t hynix_dumbssd_send_cmd (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r,
	void (*intr_handler)(void*))
{
	uint32_t ret = -1;

	switch (r->req_type) {
	case REQTYPE_READ_DUMMY:
		/* do nothing */
		break;
	case REQTYPE_WRITE:
	case REQTYPE_GC_WRITE:
	case REQTYPE_RMW_WRITE:
	case REQTYPE_META_WRITE:
		bdbm_msg ("simple_write: %llu => %llu %llu %llu", 
				r->logaddr.lpa[0],
				r->phyaddr.channel_no, 
				r->phyaddr.block_no,
				r->phyaddr.page_no);
		ret = simple_write (bdi, 
			r->phyaddr.channel_no, 
			r->phyaddr.block_no,
			r->phyaddr.page_no,
			r->fmain.kp_stt,
			r->fmain.kp_ptr);
		break;

	case REQTYPE_READ:
	case REQTYPE_GC_READ:
	case REQTYPE_RMW_READ:
	case REQTYPE_META_READ:
		bdbm_msg ("simple_read: %llu <= %llu %llu %llu", 
				r->logaddr.lpa[0],
				r->phyaddr.channel_no, 
				r->phyaddr.block_no,
				r->phyaddr.page_no);
		ret = simple_read (bdi, 
			r->phyaddr.channel_no, 
			r->phyaddr.block_no,
			r->phyaddr.page_no,
			r->fmain.kp_stt,
			r->fmain.kp_ptr);
		break;

	case REQTYPE_GC_ERASE:
		bdbm_msg ("simple_erase");
		ret = simple_erase (bdi,
			r->phyaddr.channel_no, 
			r->phyaddr.block_no);
		break;

	default:
		bdbm_error ("invalid REQTYPE (%u)", r->req_type);
		bdbm_bug_on (1);
		break;
	}

	/*intr_handler (r);*/
	return 0;
}

