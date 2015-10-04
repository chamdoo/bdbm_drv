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
#include <linux/poll.h> /* poll_table, etc. */
#include <linux/cdev.h> /* cdev_init, etc. */
#include <linux/device.h> /* class_create, device_create, etc */
#include <linux/mm.h>  /* mmap related stuff */
#include <linux/delay.h> /* msleep */
#include <linux/sched.h> /* TASK_INTERRUPTIBLE */
#include <linux/blkdev.h> /* bio */

#include "bdbm_drv.h"
#include "platform.h"
#include "debug.h"

#include "host_blockio_proxy.h"
#include "host_blockio_proxy_ioctl.h"
#include "host_blkdev.h"
#include "proxy_reqs_pool.h"


static bdbm_drv_info_t* _bdi = NULL;

bdbm_host_inf_t _host_blockio_proxy_inf = {
	.ptr_private = NULL,
	.open = blockio_proxy_open,
	.close = blockio_proxy_close,
	.make_req = blockio_proxy_make_req,
	.end_req = blockio_proxy_end_req,
};

typedef struct {
	uint32_t ref_cnt; /* # of the user-level FTLs that are linked to the kernel */
	wait_queue_head_t pollwq;
	bdbm_spinlock_t lock;
	bdbm_mutex_t mutex;
	atomic_t nr_outstanding_reqs;

	/* for mmap management */
	int64_t mmap_nr_reqs;
	bdbm_blockio_proxy_req_t* mmap_reqs_buf;
	bdbm_proxy_reqs_pool_t* reqs_pool;
} bdbm_blockio_proxy_t;

static int blockio_proxy_ioctl_init (void);
static int blockio_proxy_ioctl_exit (void);

/* This is a call-back function invoked by a block-device layer */
static void __host_blkio_make_request_fn (
	struct request_queue *q, 
	struct bio *bio)
{
	blockio_proxy_make_req (_bdi, (void*)bio);
}

/* The implement of blockio_proxy */
uint32_t blockio_proxy_open (bdbm_drv_info_t* bdi)
{
	bdbm_blockio_proxy_t* p = NULL;

	/* see if hlm_user_proxy is already created */
	if (_bdi != NULL) {
		bdbm_error ("blockio_proxy is already created");
		return -EIO;
	}

	/* create bdbm_blockio_proxy_t with zeros */
	if ((p = (bdbm_blockio_proxy_t*)bdbm_zmalloc (sizeof (bdbm_blockio_proxy_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	}

	/* initialize some variables */
	init_waitqueue_head (&p->pollwq);
	bdbm_spin_lock_init (&p->lock);
	bdbm_mutex_init (&p->mutex);
	atomic_set (&p->nr_outstanding_reqs, 0);
	p->ref_cnt = 0;
	p->mmap_nr_reqs = 31;	/* just large enough number */
	if ((p->mmap_reqs_buf = (bdbm_blockio_proxy_req_t*)kmalloc 
			(sizeof (bdbm_blockio_proxy_req_t) * p->mmap_nr_reqs, GFP_KERNEL)) == NULL) {
		/* oops! cannot allocate memory for mmap */
		bdbm_error ("oops! cannot allocate memory for mmap (%llu)", 
			sizeof (bdbm_blockio_proxy_req_t) * p->mmap_nr_reqs);
		goto fail;
	}

	/* create a request pool */
	if ((p->reqs_pool = bdbm_proxy_reqs_pool_create
			(p->mmap_nr_reqs, p->mmap_reqs_buf)) == NULL) {
		bdbm_error ("oops! bdbm_proxy_reqs_pool_create () failed");
		goto fail;
	}

	/* assign p to bdi */
	bdi->ptr_host_inf->ptr_private = (void*)p;
	_bdi = bdi;

	/* register a character device (for user-level FTL) */
	if (blockio_proxy_ioctl_init () != 0) {
		bdbm_error ("failed to register a character device");
		goto fail;
	}

	/* register a block device (for applications) */
	if (host_blkdev_register_device	(bdi, __host_blkio_make_request_fn) != 0) {
		bdbm_error ("failed to register blueDBM");
		blockio_proxy_ioctl_exit ();
		goto fail;
	}

	return 0;

fail:
	if (p->reqs_pool)
		bdbm_proxy_reqs_pool_destroy (p->reqs_pool);
	if (p->mmap_reqs_buf)
		kfree (p->mmap_reqs_buf);
	bdbm_mutex_free (&p->mutex);
	bdbm_spin_lock_destory (&p->lock);
	init_waitqueue_head (&p->pollwq);
	bdbm_free (p);
	_bdi = NULL;

	return 1;
}

void blockio_proxy_close (bdbm_drv_info_t* bdi)
{
	bdbm_blockio_proxy_t* p = NULL;
	
	if (!bdi) 
		return;
	
	if (!(p = (bdbm_blockio_proxy_t*)BDBM_HOST_PRIV (bdi)))
		return;

	/* destroy the block device */
	host_blkdev_unregister_block_device (bdi);

	/* destroy the character device */
	blockio_proxy_ioctl_exit ();

	/* before closing it, we must wait until all the on-gonging requests are
	 * finished */
	while (atomic_read (&p->nr_outstanding_reqs) > 0) {
		static int retry = 0;
		bdbm_msg ("blockio_proxy is busy... (cnt: %d)", retry);
		msleep (1000);
		retry++;
		if (retry > 10) {
			bdbm_warning ("blockio_proxy is not nicely closed (too many retries)");
			break;
		}
	}

	/* free all variables related to blockio_proxy */
	if (p->mmap_reqs_buf)
		kfree (p->mmap_reqs_buf);
	if (p->reqs_pool)
		bdbm_proxy_reqs_pool_destroy (p->reqs_pool);
	bdbm_mutex_free (&p->mutex);
	bdbm_spin_lock_destory (&p->lock);
	init_waitqueue_head (&p->pollwq);
	bdbm_free (p);

	_bdi = NULL;
}

static inline int __is_client_ready (bdbm_blockio_proxy_t* p)
{
	int i, retry_cnt = 10;

	for (i = 0; i < retry_cnt; i++) {
		bdbm_spin_lock (&p->lock);
		if (p->ref_cnt == 0) {
			bdbm_spin_unlock (&p->lock);
			/* the user-level FTL is not connected; wait for 10 seconds */
			msleep (1000);
			continue;
		}
		bdbm_spin_unlock (&p->lock);
	}

	if (i == retry_cnt)
		return 1;

	return 0;
}

static inline bdbm_blockio_proxy_req_t* __get_blockio_proxy_reqs (bdbm_blockio_proxy_t* p)
{
	bdbm_blockio_proxy_req_t* proxy_req = NULL;
	int i, retry_cnt = 10;

	for (i = 0; i < retry_cnt; i++) {
		/* is an empty slot is available? */
		bdbm_spin_lock (&p->lock);
		if ((proxy_req = bdbm_proxy_reqs_pool_alloc_item 
				(p->reqs_pool)) == NULL) {
			bdbm_spin_unlock (&p->lock);
			/* wait until there is a new empty slot */
			msleep (1000);
			continue;
		}
		bdbm_spin_unlock (&p->lock);
	}

	return proxy_req;
}

static int __encode_bio_to_proxy_req (struct bio* bio, bdbm_blockio_proxy_req_t* r)
{
	uint32_t loop = 0;
	struct bio_vec *bvec = NULL;

	r->bi_rw = bio->bi_rw;
	r->bi_sector = bio->bi_sector;
	r->bi_size = bio_sectors (bio);
	r->bi_bvec_cnt = 0;

	bio_for_each_segment (bvec, bio, loop) {
		uint8_t* mmap_vec = r->bi_bvec_data[r->bi_bvec_cnt];
		uint8_t* page_vec = (uint8_t*)page_address (bvec->bv_page);

		bdbm_bug_on (mmap_vec == NULL);
		bdbm_bug_on (page_vec == NULL);
		bdbm_memcpy (mmap_vec, page_vec, KERNEL_PAGE_SIZE);
		r->bi_bvec_cnt++;

		if (r->bi_bvec_cnt >= BDBM_PROXY_MAX_VECS) {
			/* NOTE: this is an impossible case unless kernel parameters are changed */
			bdbm_error ("oops! # of vectors in bio is larger than %u", 
				BDBM_PROXY_MAX_VECS);
			break;
		}
	}

	return 0;
}

void blockio_proxy_make_req (bdbm_drv_info_t* bdi, void* req)
{
	struct bio* bio = (struct bio*)req;
	bdbm_blockio_proxy_t* p = (bdbm_blockio_proxy_t*)BDBM_HOST_PRIV (bdi);
	bdbm_blockio_proxy_req_t* proxy_req = NULL;

	bdbm_mutex_lock (&p->mutex);

	/* see if the user-level FTL is connected to the kernel */
	if (__is_client_ready (p) != 0) {
		bdbm_warning ("oops! the user-level FTL is not ready");
		bio_endio (bio, 0);
		bdbm_mutex_unlock (&p->mutex);
		return;
	}

	/* send an incoming request to the user-level FTL */
	/* (1) get an empty mmap_req slot */
	if ((proxy_req = __get_blockio_proxy_reqs (p)) == NULL) {
		bdbm_warning ("oops! mmap_reqs is full");
		bio_endio (bio, 0);
		bdbm_mutex_unlock (&p->mutex);
		return;
	}

	/* (2) encode it to mapped-memory */
	if (__encode_bio_to_proxy_req (bio, proxy_req) != 0) {
		bdbm_proxy_reqs_pool_free_item (p->reqs_pool, proxy_req);
		bdbm_warning ("oops! mmap_reqs_buf is full");
		bdbm_mutex_unlock (&p->mutex);
		return;
	}

	proxy_req->bio = (void*)bio;
	atomic_inc (&p->nr_outstanding_reqs);

	/* trigger a poller */
	wake_up_interruptible (&(p->pollwq));

	bdbm_mutex_unlock (&p->mutex);
}

void blockio_proxy_end_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* req)
{
	bdbm_blockio_proxy_req_t* r = (bdbm_blockio_proxy_req_t*)req;
	bdbm_blockio_proxy_t* p = (bdbm_blockio_proxy_t*)BDBM_HOST_PRIV (bdi);

	bdbm_mutex_lock (&p->mutex);
	if (r->ret == 0) {
		bio_endio (r->bio, 0);
	} else {
		bio_io_error (r->bio);
	}
	atomic_dec (&p->nr_outstanding_reqs);
	bdbm_mutex_unlock (&p->mutex);
}


/*
 * For the interaction with user-level application
 */
static long blockio_proxy_fops_ioctl (struct file *filp, unsigned int cmd, unsigned long arg);
static unsigned int blockio_proxy_fops_poll (struct file *filp, poll_table *poll_table);
static void mmap_open (struct vm_area_struct *vma);
static void mmap_close (struct vm_area_struct *vma);
static int blockio_proxy_fops_mmap (struct file *filp, struct vm_area_struct *vma);
static int blockio_proxy_fops_create (struct inode *inode, struct file *filp);
static int blockio_proxy_fops_release (struct inode *inode, struct file *filp);

static struct vm_operations_struct mmap_vm_ops = {
	.open = mmap_open,
	.close = mmap_close,
};

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.mmap = blockio_proxy_fops_mmap, 
	.open = blockio_proxy_fops_create,
	.release = blockio_proxy_fops_release,
	.poll = blockio_proxy_fops_poll,
	.unlocked_ioctl = blockio_proxy_fops_ioctl,
	.compat_ioctl = blockio_proxy_fops_ioctl,
};

void mmap_open (struct vm_area_struct *vma)
{
	bdbm_msg ("mmap_open: virt %lx, phys %lx",
		vma->vm_start, 
		vma->vm_pgoff << PAGE_SHIFT);
}

void mmap_close (struct vm_area_struct *vma)
{
	bdbm_msg ("mmap_close");
}

static int blockio_proxy_fops_mmap (struct file *filp, struct vm_area_struct *vma)
{
	bdbm_drv_info_t* bdi = (bdbm_drv_info_t*)filp->private_data;
	bdbm_blockio_proxy_t* p = (bdbm_blockio_proxy_t*)BDBM_HOST_PRIV (bdi);
	uint64_t size = vma->vm_end - vma->vm_start;

	if (p == NULL) {
		bdbm_warning ("blockio_proxy is not created yet");
		return -EINVAL;
	}

	if (size > p->mmap_nr_reqs * sizeof (bdbm_blockio_proxy_t)) {
		bdbm_warning ("size > p->mmap_nr_reqs: %llu > %llu", 
			size, p->mmap_nr_reqs * sizeof (bdbm_blockio_proxy_t));
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_noncached (vma->vm_page_prot);
	vma->vm_pgoff = __pa(p->mmap_reqs_buf) >> PAGE_SHIFT;

	if (remap_pfn_range (vma, vma->vm_start, 
			__pa(p->mmap_reqs_buf) >> PAGE_SHIFT,
			size, vma->vm_page_prot)) {
		return -EAGAIN;
	}

	vma->vm_ops = &mmap_vm_ops;
	vma->vm_private_data = p;	/* bdbm_blockio_proxy_t */
	mmap_open (vma);

	bdbm_msg ("blockio_proxy_fops_mmap is called (%lu)", vma->vm_end - vma->vm_start);

	return 0;
}

static int blockio_proxy_fops_create (struct inode *inode, struct file *filp)
{
	bdbm_drv_info_t* bdi = _bdi;
	bdbm_blockio_proxy_t* p = NULL;

	/* see if other FTL are already connected to the kernel */
	if (filp->private_data != NULL) {
		bdbm_error ("filp->private_data is *NOT* NULL");
		return -EBUSY;
	}

	/* is bdbm_blockio_proxy_t created? */
	if (bdi == NULL) {
		bdbm_error ("the kernel is not initialized yet");
		return -EBUSY;
	}
	p = (bdbm_blockio_proxy_t*)BDBM_HOST_PRIV (bdi);

	bdbm_spin_lock (&p->lock);
	if (p->ref_cnt != 0) {
		bdbm_error ("The user-level FTL is already attached to the kernel (ref_cnt: %u)", p->ref_cnt);
		bdbm_spin_unlock (&p->lock);
		return -EBUSY;
	}
	bdbm_spin_unlock (&p->lock);

	/* ok! assign bdbm_blockio_proxy_ioctl to private_data */
	filp->private_data = (void *)_bdi;
	filp->f_mode |= FMODE_WRITE;

	bdbm_msg ("[%s] The user-level FTL is attached to the kernel succesfully", __FUNCTION__);

	return 0;
}

static int blockio_proxy_fops_release (struct inode *inode, struct file *filp)
{
	bdbm_drv_info_t* bdi = (bdbm_drv_info_t*)filp->private_data;
	bdbm_blockio_proxy_t* p = (bdbm_blockio_proxy_t*)BDBM_HOST_PRIV (bdi);

	/* bdbm_blockio_proxy_ioctl is not open before */
	if (p == NULL) {
		bdbm_warning ("attempt to close blockio_proxy which was not open");
		return 0;
	}

	/* reset private_data */
	filp->private_data = (void *)NULL;

	bdbm_msg ("blockio_proxy_fops_release is done");

	return 0;
}

static unsigned int blockio_proxy_fops_poll (struct file *filp, poll_table *poll_table)
{
	bdbm_drv_info_t* bdi = (bdbm_drv_info_t*)filp->private_data;
	bdbm_blockio_proxy_t* p = (bdbm_blockio_proxy_t*)BDBM_HOST_PRIV (bdi);
	unsigned int mask = 0;

	if (p == NULL) {
		bdbm_error ("bdbm_blockio_proxy_ioctl is not created");
		return 0;
	}

	poll_wait (filp, &p->pollwq, poll_table);

	/* are there any outstanding requests? */
	if (atomic_read (&p->nr_outstanding_reqs) > 0) {
		mask |= POLLIN | POLLRDNORM; 
	}

	return mask;
}

#include <asm/uaccess.h>
static long blockio_proxy_fops_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	bdbm_drv_info_t* bdi = (bdbm_drv_info_t*)filp->private_data;
	bdbm_blockio_proxy_t* p = (bdbm_blockio_proxy_t*)BDBM_HOST_PRIV (bdi);
	bdbm_host_inf_t* h = (bdbm_host_inf_t*)BDBM_GET_HOST_INF (bdi);
	int ret = 0;

	/* see if blockio_proxy is valid or not */
	if (p == NULL) {
		bdbm_error ("bdbm_blockio_proxy_ioctl is not created");
		return -ENOTTY;
	}

	/* handle a command from user applications */
	switch (cmd) {
	case BDBM_BLOCKIO_PROXY_IOCTL_DONE:
		if (h != NULL && h->end_req != NULL) {
			int req_id = -1;
			int __user* req_id_ur = (int __user*)arg;
			bdbm_blockio_proxy_req_t* r = NULL;

			/* copy ur_id from user-level FTL */
			copy_from_user (&req_id, req_id_ur, sizeof (int));
			bdbm_bug_on (req_id < 0);
			bdbm_bug_on (req_id >= p->mmap_nr_reqs);

			/* get req */
			r = &p->mmap_reqs_buf[req_id];
			bdbm_bug_on (r->stt != PROXY_REQ_STT_USER_DONE);

			/* call end_req */
			h->end_req (bdi, (bdbm_hlm_req_t*)r);
		}
		break;

	default:
		bdbm_warning ("invalid command code");
		ret = -ENOTTY;
	}

	return ret;
}


/*
 * For the registration of blockio_proxy as a character device
 */
static dev_t devnum = 0; 
static struct cdev c_dev;
static struct class *cl = NULL;
static int FIRST_MINOR = 0;
static int MINOR_CNT = 1;

/* register a bdbm_blockio_proxy_ioctl driver */
static int blockio_proxy_ioctl_init (void)
{
	int ret = -1;
	struct device *dev_ret = NULL;

	if ((ret = alloc_chrdev_region (&devnum, FIRST_MINOR, MINOR_CNT, BDBM_BLOCKIO_PROXY_IOCTL_NAME)) != 0) {
		bdbm_error ("bdbm_blockio_proxy_ioctl registration failed: %d\n", ret);
		return ret;
	}
	cdev_init (&c_dev, &fops);

	if ((ret = cdev_add (&c_dev, devnum, MINOR_CNT)) < 0) {
		bdbm_error ("bdbm_blockio_proxy_ioctl registration failed: %d\n", ret);
		return ret;
	}

	if (IS_ERR (cl = class_create (THIS_MODULE, "char"))) {
		bdbm_error ("bdbm_blockio_proxy_ioctl registration failed: %d\n", MAJOR(devnum));
		cdev_del (&c_dev);
		unregister_chrdev_region (devnum, MINOR_CNT);
		return PTR_ERR (cl);
	}

	if (IS_ERR (dev_ret = device_create (cl, NULL, devnum, NULL, BDBM_BLOCKIO_PROXY_IOCTL_NAME))) {
		bdbm_error ("bdbm_blockio_proxy_ioctl registration failed: %d\n", MAJOR(devnum));
		class_destroy (cl);
		cdev_del (&c_dev);
		unregister_chrdev_region (devnum, MINOR_CNT);
		return PTR_ERR (dev_ret);
	}

	bdbm_msg ("bdbm_blockio_proxy_ioctl is installed: %s (major:%d minor:%d)", 
		BDBM_BLOCKIO_PROXY_IOCTL_DEVNAME, 
		MAJOR(devnum), MINOR(devnum));

	return 0;
}

/* remove a bdbm_db_stub driver */
static int blockio_proxy_ioctl_exit (void)
{
	if (cl == NULL || devnum == 0) {
		bdbm_warning ("bdbm_blockio_proxy_ioctl is not installed yet");
		return 1;
	}

	/* get rid of bdbm_blockio_proxy_ioctl */
	device_destroy (cl, devnum);
    class_destroy (cl);
    cdev_del (&c_dev);
    unregister_chrdev_region (devnum, MINOR_CNT);

	bdbm_msg ("bdbm_blockio_proxy_ioctl is removed: %s (%d %d)", 
		BDBM_BLOCKIO_PROXY_IOCTL_DEVNAME, 
		MAJOR(devnum), MINOR(devnum));

	return 0;
}

/* NOTE: We create fake interface to avoid compile errors. Do not use them for
 * any other purposes! */
bdbm_ftl_inf_t _ftl_block_ftl, _ftl_page_ftl, _ftl_dftl, _ftl_no_ftl;
bdbm_hlm_inf_t _hlm_dftl_inf, _hlm_buf_inf, _hlm_nobuf_inf, _hlm_rsd_inf;
bdbm_llm_inf_t _llm_mq_inf, _llm_noq_inf;
bdbm_host_inf_t _host_blockio_inf;

