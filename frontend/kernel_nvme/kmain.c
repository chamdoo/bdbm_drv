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

bdbm_drv_info_t* _bdi = NULL;

/*int ridx = 0;*/
/*int widx = 0;*/

#define BITS_PER_SLICE 	6
#define BITS_PER_WU 	7
#define BITS_PER_DIE	6


#include <linux/nvme.h>
#include <linux/blk-mq.h>

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

int simple_read (bdbm_drv_info_t* bdi, int die, int block, int wu)
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

	if (_bdi == NULL)
		return 0;

	rq = blk_mq_alloc_request(bdi->q, 0, 0);
	if (IS_ERR(rq))
		return -ENOMEM;

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret)
		goto out;

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
	if (ret != 0)
		bdbm_msg ("ret = %u", ret);

	{
		char* tmp = (char*)ubuffer;
		bdbm_msg ("data: %x %x %x %x", tmp[0], tmp[1], tmp[2], tmp[3]);
	}

out:
	kfree (ubuffer);
	blk_mq_free_request(rq);

	return 0;
}

int simple_write (bdbm_drv_info_t* bdi, int die, int block, int wu)
{
	int ret;
	struct request *rq;
	unsigned bufflen = 64 * 4096;
	struct nvme_command cmd;
	void* ubuffer = kzalloc (bufflen, GFP_KERNEL);
	char* ubuffer_char = (char*)ubuffer;

	int req_len = 63;
	int req_ofs = block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  die << (BITS_PER_WU + BITS_PER_SLICE) |
				  wu << (BITS_PER_SLICE);

	if (_bdi == NULL)
		return 0;

	ubuffer_char[0] = die+1;
	ubuffer_char[1] = block+1;
	ubuffer_char[2] = wu+1;
	ubuffer_char[3] = 0;

	rq = blk_mq_alloc_request(bdi->q, 1, 0);
	if (IS_ERR(rq))
		return -ENOMEM;

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret)
		goto out;

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
	if (ret != 0)
		bdbm_msg ("ret = %u", ret);

	/*{*/
	/*char* tmp = (char*)ubuffer;*/
	/*bdbm_msg ("data: %x %x %x %x", tmp[0], tmp[1], tmp[2], tmp[3]);*/
	/*}*/

out:
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
	__le32* ubuffer_char = (__le32*)ubuffer;

	__u32 req_ofs = block << (BITS_PER_DIE + BITS_PER_WU + BITS_PER_SLICE) |
				  die << (BITS_PER_WU + BITS_PER_SLICE);

	if (_bdi == NULL)
		return 0;

	ubuffer_char[0] = 
		(req_ofs >> 24 & 0x000000FF) | 
		(req_ofs >>  8 & 0x0000FF00) | 
		(req_ofs <<  8 & 0x00FF0000) |  
		(req_ofs << 24 & 0xFF000000) ;

	bdbm_msg ("%x %x", ubuffer_char[0], req_ofs);

	rq = blk_mq_alloc_request(bdi->q, 1, 0);
	if (IS_ERR(rq))
		return -ENOMEM;

	rq->cmd_type = REQ_TYPE_DRV_PRIV;

	ret = blk_rq_map_kern (bdi->q, rq, ubuffer, bufflen, GFP_KERNEL);
	if (ret)
		goto out;

	memset(&cmd, 0, sizeof(cmd));
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

	/*{*/
	/*char* tmp = (char*)ubuffer;*/
	/*bdbm_msg ("data: %x %x %x %x", tmp[0], tmp[1], tmp[2], tmp[3]);*/
	/*}*/

out:
	kfree (ubuffer);
	blk_mq_free_request(rq);

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

static long nvm_ctl_ioctl(struct file *file, uint cmd, unsigned long arg)
{
	struct user_cmd c;

	if (_bdi == NULL) {
		return 0;
	}

	switch (cmd) {
	case TEST_IOCTL_READ:
		copy_from_user (&c, (struct user_cmd __user*)arg, sizeof(struct user_cmd));
		bdbm_msg ("simple_read: %d %d %d", c.die, c.block, c.wu);
		simple_read (_bdi, c.die, c.block, c.wu);
		break;
	case TEST_IOCTL_WRITE:
		copy_from_user (&c, (struct user_cmd __user*)arg, sizeof(struct user_cmd));
		bdbm_msg ("simple_write: %d %d %d", c.die, c.block, c.wu);
		simple_write (_bdi, c.die, c.block, c.wu);
		break;
	case TEST_IOCTL_ERASE:
		copy_from_user (&c, (struct user_cmd __user*)arg, sizeof(struct user_cmd));
		bdbm_msg ("simple_erase: %d %d", c.die, c.block);
		simple_erase (_bdi, c.die, c.block);
		break;
	default:
		bdbm_msg ("default");
		break;
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
