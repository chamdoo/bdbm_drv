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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "debug.h"
#include "dm_dumbssd.h"
#include "dev_params.h"
#include "utime.h"
#include "umemory.h"
#include "dev_hynix_nvme.h"

#define NR_DIES 64


#define ENABLE_SEQ_DBG

/* global data structure */
dumb_ssd_dev_t _dumb_dev;

bdbm_drv_info_t* _bdi_dm = NULL;
/*bdbm_drv_info_t _bdi;*/

bdbm_sema_t die_locks[NR_DIES];

/* interface for dm */
bdbm_dm_inf_t _bdbm_dm_inf = {
	.ptr_private = NULL,
	.probe = dm_dumbssd_probe,
	.open = dm_dumbssd_open,
	.close = dm_dumbssd_close,
	.make_req = dm_dumbssd_make_req,
	.make_reqs = NULL,
	.end_req = dm_dumbssd_end_req,
	.load = NULL,
	.store = NULL,
};

/* private data structure for dm */
typedef struct {
#if defined(ENABLE_SEQ_DBG)
	bdbm_sema_t dbg_seq;
#endif
} dm_dumbssd_private_t;


/* interrupt handler */
static void io_done (void* arg)
{
	bdbm_llm_req_t* r = (bdbm_llm_req_t*)arg;
	bdbm_drv_info_t* bdi = _bdi_dm;

	bdi->ptr_dm_inf->end_req (bdi, r);
}

uint32_t dm_dumbssd_probe (bdbm_drv_info_t* bdi, bdbm_device_params_t* params)
{
	dm_dumbssd_private_t* p = NULL;

	/* setup NAND parameters according to users' inputs TODO: all the
	 * parameters will be configured according to the information from the
	 * device (get_capabilities) */
	*params = get_default_device_params ();

	display_device_params (params);

	/* create a private structure for ramdrive */
	if ((p = (dm_dumbssd_private_t*)bdbm_malloc_atomic 
			(sizeof (dm_dumbssd_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail;
	}

#if defined(ENABLE_SEQ_DBG)
	bdbm_sema_init (&p->dbg_seq);
#endif

	/* OK! keep private info */
	bdi->ptr_dm_inf->ptr_private = (void*)p;
	bdbm_msg ("[dm_dumbssd_probe] probe done!");
	return 0;

fail:
	return -1;
}

uint32_t dm_dumbssd_open (bdbm_drv_info_t* bdi)
{
	dm_dumbssd_private_t * p = BDBM_DM_PRIV (bdi);

	bdbm_msg ("[dm_dumbssd_open] open done! (%p)", p);

	return 0;
}

void dm_dumbssd_close (bdbm_drv_info_t* bdi)
{
	dm_dumbssd_private_t* p = BDBM_DM_PRIV (bdi);

	bdbm_msg ("[dm_dumbssd_close] closed!");

	bdbm_free_atomic (p);
}

uint32_t dm_dumbssd_make_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* r)
{
	uint32_t ret;

	dm_dumbssd_private_t* p = BDBM_DM_PRIV (bdi);

#if defined(ENABLE_SEQ_DBG)
	bdbm_sema_lock (&p->dbg_seq);
#endif

	if ((ret = dev_hynix_nvme_submit_io (&_dumb_dev, r, io_done)) != 0) {
		bdbm_error ("dev_hynix_nvme_submit_io() failed");
	}

#if defined(ENABLE_SEQ_DBG)
	bdbm_sema_unlock (&p->dbg_seq);
#endif

	return ret;
}

void dm_dumbssd_end_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* r)
{
	bdbm_bug_on (r == NULL);

	bdi->ptr_llm_inf->end_req (bdi, r);
}

int bdbm_dm_init (bdbm_drv_info_t* bdi)
{
	/* see if bdi is valid or not */
	if (bdi == NULL) {
		bdbm_warning ("bdi is NULL");
		return 1;
	}

	if (_bdi_dm != NULL) {
		bdbm_warning ("dm_stub is already used by other clients");
		return 1;
	}

	/* initialize global variables */
	_bdi_dm = bdi;
	return 0;
}
EXPORT_SYMBOL (bdbm_dm_init);

void bdbm_dm_exit (bdbm_drv_info_t* bdi)
{
	_bdi_dm = NULL;
}
EXPORT_SYMBOL (bdbm_dm_exit);

bdbm_dm_inf_t* bdbm_dm_get_inf (bdbm_drv_info_t* bdi)
{
	if (_bdi_dm == NULL) {
		bdbm_warning ("_bdi_dm is not initialized yet");
		return NULL;
	}
	return &_bdbm_dm_inf;
}
EXPORT_SYMBOL (bdbm_dm_get_inf);


/* 
 * A set of functions for module management 
 */
static void ioctl_io_done (void* arg)
{
	hd_req_t* hc = (hd_req_t*)arg;
	if (hc) {
		bdbm_sema_unlock (&die_locks[hc->die]);
		kfree (hc);
	}
}

static long nvm_ctl_ioctl(struct file *file, uint cmd, unsigned long arg)
{
	dumbssd_user_cmd_t c;
	hd_req_t* hc = kzalloc (sizeof (hd_req_t), GFP_KERNEL);

	copy_from_user (&c, (dumbssd_user_cmd_t __user*)arg, sizeof(dumbssd_user_cmd_t));
	bdbm_sema_lock (&die_locks[c.die]);

	bdbm_bug_on (_dumb_dev.q == NULL);
	bdbm_bug_on (_dumb_dev.gd == NULL);
	
	hc->dev = &_dumb_dev;
	hc->r = NULL;
	hc->die = c.die;
	hc->block = c.block;
	hc->wu = c.wu;
	hc->buffer = kzalloc (4096*64, GFP_KERNEL);
	hc->kp_ptr = NULL;
	hc->intr_handler = ioctl_io_done;

	switch (cmd) {
	case TEST_IOCTL_READ:
		hc->rw = READ;
		bdbm_msg ("IOCTL_READ: %d %d %d", c.die, c.block, c.wu);
		simple_read (&_dumb_dev, hc);
		break;
	case TEST_IOCTL_WRITE:
		bdbm_msg ("IOCTL_WRITE: %d %d %d", c.die, c.block, c.wu);
		hc->rw = WRITE;
		hc->buffer[0] = c.die + 1;
		hc->buffer[1] = c.block + 1;
		hc->buffer[2] = c.wu + 1;
		simple_write (&_dumb_dev, hc);
		break;
	case TEST_IOCTL_ERASE:
		hc->rw = 0xff;
		bdbm_msg ("IOCTL_ERASE: %d %d %d", c.die, c.block, c.wu);
		simple_erase (&_dumb_dev, hc);
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

static struct block_device_operations _bdops = {
	.owner = THIS_MODULE,
};

MODULE_ALIAS_MISCDEV (MISC_DYNAMIC_MINOR);

static blk_qc_t dummy_fn (struct request_queue *q, struct bio *bio)
{
	if (bio)
		bio_endio (bio);
	return BLK_QC_T_NONE; /* for no polling */
}

int bdbm_register (struct request_queue* q, char* disk_name)
{
	uint64_t capacity = 0;
	
	bdbm_bug_on (q == NULL);
	bdbm_bug_on (disk_name == NULL);

	_dumb_dev.q = q;

	if (!(_dumb_dev.queue = blk_alloc_queue_node (GFP_KERNEL, q->node))) {
		bdbm_error ("blk_alloc_queue_node() failed");
		return -ENOMEM;
	}

	blk_queue_make_request (_dumb_dev.queue, dummy_fn);
	blk_queue_logical_block_size (_dumb_dev.queue, 512);
	blk_queue_io_min (_dumb_dev.queue, 4096);
	blk_queue_io_opt (_dumb_dev.queue, 4096);

	_dumb_dev.queue->limits.discard_granularity = 4096;
	_dumb_dev.queue->limits.max_discard_sectors = UINT_MAX;
	queue_flag_set_unlocked (QUEUE_FLAG_DISCARD, _dumb_dev.queue);

	if (!(_dumb_dev.gd = alloc_disk (0))) {
		bdbm_msg ("alloc_disk() failed");
		return -ENOMEM;
	}
	_dumb_dev.gd->flags = GENHD_FL_EXT_DEVT;

	_dumb_dev.gd->major = 0;
	_dumb_dev.gd->first_minor = 0;
	_dumb_dev.gd->fops = &_bdops;
	_dumb_dev.gd->queue = _dumb_dev.queue;
	_dumb_dev.gd->private_data = NULL;
	strcpy (_dumb_dev.gd->disk_name, disk_name);

	/* setup disk capacity */
	capacity = 4 * 1024 * 1024;
	capacity = (capacity) - capacity/10;
	set_capacity (_dumb_dev.gd, capacity);
	add_disk (_dumb_dev.gd);

	bdbm_msg ("Hynix DumbSSD is registered");

	return 0;
}
EXPORT_SYMBOL (bdbm_register);

void bdbm_unregister (char* disk_name)
{
	del_gendisk (_dumb_dev.gd);
	blk_cleanup_queue (_dumb_dev.queue);
	put_disk (_dumb_dev.gd);

	bdbm_msg ("Hynix DumbSSD is unregistered");
}
EXPORT_SYMBOL(bdbm_unregister);


/* module init & exit */
static int __init dm_dumbssd_init (void)
{
	int ret, loop;

	for (loop = 0; loop < NR_DIES; loop++)
		bdbm_sema_init (&die_locks[loop]);

	if ((ret = misc_register (&_nvm_misc)))
		bdbm_error ("bdbm_register failed for control device");
	return 0;
}

static void __exit dm_dumbssd_exit (void)
{
	misc_deregister (&_nvm_misc);
}


MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("DUMBSSD Device Wrapper");
MODULE_LICENSE ("GPL");

module_init (dm_dumbssd_init);
module_exit (dm_dumbssd_exit);
