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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "bdbm_drv.h"
#include "debug.h"
#include "blkio.h"
#include "devices.h"
#include "umemory.h"

#include <linux/nvme.h>
#include <linux/blk-mq.h>

#define BITS_PER_SLICE 	6
#define BITS_PER_WU 	7
#define BITS_PER_DIE	6

#define USE_ASYNC

bdbm_drv_info_t* _bdi = NULL;

struct hd_req {
	bdbm_drv_info_t* bdi;
	bdbm_llm_req_t* r;
	int rw;
	uint64_t die;
	uint64_t block;
	uint64_t wu;
	uint8_t* kp_ptr;
	void (*intr_handler)(void*);
};

void print_bio (struct bio* bio) 
{
	/*
	bdbm_msg ("bio->bi_iter.bi_sector = %d", (int)bio->bi_iter.bi_sector);
	bdbm_msg ("bio->bi_iter.bi_size = %d", (int)bio->bi_iter.bi_size);
	bdbm_msg ("bio->bi_rw = %d", (int)bio->bi_rw);
	bdbm_msg ("bio->bi_private = %p", bio->bi_private);
	bdbm_msg ("bio->bi_end_io = %p", bio->bi_end_io);
	*/
}

static void nvme_nvm_end_io (struct request *rq, int error)
{
	struct hd_req* hdr = rq->end_io_data;

	if (hdr->rw == READ) {
		bdbm_msg (" ==> %u %u %u %u", hdr->kp_ptr[0], hdr->kp_ptr[1], hdr->kp_ptr[2], hdr->kp_ptr[3]);
	}

	if (rq->bio && rq->bio->bi_end_io) {
		rq->bio->bi_end_io (rq->bio);
	}
	if (rq->cmd)
		kfree (rq->cmd);
	if (hdr)
		kfree (hdr);
	if (hdr->kp_ptr)
		kfree (hdr->kp_ptr);
	if (rq)
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

	bdbm_msg ("READ: %llu %llu %llu => %u (%x)", 
		hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);

	/* [STEP1] setup bio */
	/*bio = bio_map_kern (bdi->q, hdr->kp_ptr, 64*4096, GFP_NOIO);*/
	bio = bio_copy_kern (bdi->q, hdr->kp_ptr, 64*4096, GFP_NOIO, 1);
	bio->bi_rw = READ;
	print_bio (bio);

	/* [STEP2] alloc request */
	rq = blk_mq_alloc_request(bdi->q, 0, 0);
	if (IS_ERR(rq)) {
		bdbm_error ("blk_mq_alloc_request");
		bdbm_bug_on (1);
		return -ENOMEM;
	}
	rq->cmd_type = REQ_TYPE_DRV_PRIV;
	rq->ioprio = bio_prio(bio);

	if (bio_has_data(bio)) {
		rq->nr_phys_segments = bio_phys_segments(bdi->q, bio);
	}
	rq->__data_len = bio->bi_iter.bi_size;
	rq->bio = rq->biotail = bio;

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

#ifdef USE_ASYNC
	blk_execute_rq_nowait (bdi->q, bdi->gd, rq, 0, nvme_nvm_end_io);
#else
	blk_execute_rq (bdi->q, bdi->gd, rq, 0);
	bdbm_msg (" ==> %u %u %u %u", hdr->kp_ptr[0], hdr->kp_ptr[1], hdr->kp_ptr[2], hdr->kp_ptr[3]);
	bio->bi_end_io (bio);
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

	hdr->kp_ptr[0] = hdr->block;
	hdr->kp_ptr[1] = hdr->die;
	hdr->kp_ptr[2] = hdr->wu;
	bdbm_msg ("WRITE: %llu %llu %llu => %u (%x)", hdr->block, hdr->die, hdr->wu, req_ofs, req_ofs);

	/* setup bio */
	/*bio = bio_map_kern (bdi->q, hdr->kp_ptr, 64*4096, GFP_NOIO);*/
	bio = bio_copy_kern (bdi->q, hdr->kp_ptr, 64*4096, GFP_NOIO, 0);
	bio->bi_rw = WRITE;
	print_bio (bio);

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
	bio->bi_end_io (bio);
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
	struct nvme_command* cmd = kzalloc (sizeof (struct nvme_command), GFP_KERNEL);
	__u32 req_ofs = hdr->block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  hdr->die << (BITS_PER_WU + BITS_PER_SLICE);
	__le64* ubuffer_64 = (__le64*)hdr->kp_ptr;
	
	bdbm_msg ("ERASE: %llu %llu => %u (%x)", hdr->block, hdr->die, req_ofs, req_ofs);

	ubuffer_64[1] = req_ofs;

	/* setup bio */
	/*bio = bio_map_kern (bdi->q, hdr->kp_ptr, 64*4096, GFP_NOIO);*/
	bio = bio_copy_kern (bdi->q, hdr->kp_ptr, 64*4096, GFP_NOIO, 0);
	bio->bi_rw = WRITE;
	print_bio (bio);

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
	bio->bi_end_io (bio);
	rq->end_io_data = hdr;
	nvme_nvm_end_io (rq, 0);
#endif

	return 0;
}

int bdbm_register (struct request_queue* q, char* disk_name)
{
	bdbm_msg ("bdbm_register (%s)", disk_name);

	/* create bdi with default parameters */
	if ((_bdi = bdbm_drv_create ()) == NULL) {
		bdbm_error ("[kmain] bdbm_drv_create () failed");
		return -ENXIO;
	}

	/* open the device */
	if (bdbm_dm_init (_bdi) != 0) {
		bdbm_error ("[kmain] bdbm_dm_init () failed");
		return -ENXIO;
	}

	/* attach the host & the device interface to the bdbm */
	if (bdbm_drv_setup (_bdi, &_blkio_inf, bdbm_dm_get_inf (_bdi)) != 0) {
		bdbm_error ("[kmain] bdbm_drv_setup () failed");
		return -ENXIO;
	}

	/* for nvme */
	_bdi->q = q;
	if (disk_name)
		strcpy (_bdi->disk_name, disk_name);
	else
		strcpy (_bdi->disk_name, "blueDBM");
	/* end */

	/* run it */
	if (bdbm_drv_run (_bdi) != 0) {
		bdbm_error ("[kmain] bdbm_drv_run () failed");
		return -ENXIO;
	}

#if 0
	/* send simple read request */
	bdbm_msg ("begin - simple_read\n");
	{
		int i = 0;
		for (i = 0; i < 1000; i++) {
			simple_read (_bdi);
		}
	}
	bdbm_msg ("done - simple_read\n");
	/* end */
#endif

	return 0;
}
EXPORT_SYMBOL(bdbm_register);

void bdbm_unregister (char* disk_name)
{
	/* stop running layers */
	bdbm_drv_close (_bdi);

	/* close the device */
	bdbm_dm_exit (_bdi);

	/* remove bdbm_drv */
	bdbm_drv_destroy (_bdi);

	_bdi = NULL;

	bdbm_msg ("bdbm_register (%s)", disk_name);
}
EXPORT_SYMBOL(bdbm_unregister);


struct user_cmd {
	int die;
	int block;
	int wu;
};

#define TEST_IOCTL_READ _IO('N', 0x01)
#define TEST_IOCTL_WRITE _IO('N', 0x02)
#define TEST_IOCTL_ERASE _IO('N', 0x03)

#if 0
static long nvm_ctl_ioctl(struct file *file, uint cmd, unsigned long arg)
{
	struct user_cmd c;
	struct hd_req* hc = kzalloc (sizeof (struct hd_req), GFP_KERNEL);

	if (_bdi == NULL)
		return 0;

	copy_from_user (&c, (struct user_cmd __user*)arg, sizeof(struct user_cmd));
	
	hc->bdi = _bdi;
	hc->r = NULL;
	hc->die = c.die;
	hc->block = c.block;
	hc->wu = c.wu;
	hc->kp_ptr = kzalloc (4096*64, GFP_KERNEL);
	hc->intr_handler = NULL;

	switch (cmd) {
	case TEST_IOCTL_READ:
		hc->rw = READ;
		simple_read (_bdi, hc);
		break;
	case TEST_IOCTL_WRITE:
		hc->rw = WRITE;
		simple_write (_bdi, hc);
		break;
	case TEST_IOCTL_ERASE:
		hc->rw = 0xff;
		simple_erase (_bdi, hc);
		break;
	default:
		bdbm_msg ("default");
		break;
	}

	return 0;
}
#endif

static long nvm_ctl_ioctl(struct file *file, uint cmd, unsigned long arg)
{
	struct user_cmd c;
	struct hd_req* hc = NULL;
	int die = 0;

	if (_bdi == NULL)
		return 0;

	copy_from_user (&c, (struct user_cmd __user*)arg, sizeof(struct user_cmd));

	for (die = 0; die < 64; die++) {
		struct hd_req* hc = kzalloc (sizeof (struct hd_req), GFP_KERNEL);

		hc->bdi = _bdi;
		hc->r = NULL;
		hc->die = die;
		hc->block = c.block;
		hc->wu = c.wu;
		hc->kp_ptr = kzalloc (4096*64, GFP_KERNEL);
		hc->intr_handler = NULL;

		switch (cmd) {
		case TEST_IOCTL_READ:
			hc->rw = READ;
			simple_read (_bdi, hc);
			break;
		case TEST_IOCTL_WRITE:
			hc->rw = WRITE;
			simple_write (_bdi, hc);
			break;
		case TEST_IOCTL_ERASE:
			hc->rw = 0xff;
			simple_erase (_bdi, hc);
			break;
		default:
			bdbm_msg ("default");
			break;
		}
	}

	return 0;
}


static const struct file_operations _ctl_fops = {
	.open = nonseekable_open,
	.unlocked_ioctl = nvm_ctl_ioctl,
	.owner = THIS_MODULE,
};

static struct miscdevice _nvm_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "kernel_nvme",
	.nodename	= "kernel_nvme/control",
	.fops		= &_ctl_fops,
};

MODULE_ALIAS_MISCDEV(MISC_DYNAMIC_MINOR);

static int __init bdbm_drv_init (void)
{
	int ret;

	ret = misc_register(&_nvm_misc);
	if (ret)
		pr_err("nvm: misc_register failed for control device");

	/*bdbm_register (NULL, NULL);*/

	return 0;
}

static void __exit bdbm_drv_exit(void)
{
	misc_deregister(&_nvm_misc);

	/*bdbm_unregister(NULL);*/
}

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("BlueDBM Device Driver");
MODULE_LICENSE ("GPL");

module_init (bdbm_drv_init);
module_exit (bdbm_drv_exit);
