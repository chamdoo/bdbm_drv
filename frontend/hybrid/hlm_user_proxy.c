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

#include "bdbm_drv.h"
#include "platform.h"
#include "debug.h"

#include "hlm_user_proxy.h"
#include "hlm_user_proxy_ioctl.h"


bdbm_hlm_inf_t _hlm_user_prox_inf = {
	.ptr_private = NULL,
	.create = hlm_user_proxy_create,
	.destroy = hlm_user_proxy_destroy,
	.make_req = hlm_user_proxy_make_req,
	.end_req = hlm_user_proxy_end_req,
};

typedef struct {
	uint32_t ref_cnt; /* # of the user-level FTLs that are linked to the kernel */

	wait_queue_head_t pollwq;
	uint8_t* mmap_shared;
	uint64_t mmap_shared_size;

	bdbm_spinlock_t lock;
	bdbm_mutex_t mutex;
} bdbm_hlm_user_proxy_t;

/* ugly -- for sharing it with ioctl */
bdbm_hlm_user_proxy_t* _hlm_user_proxy = NULL;


/* The implement of hlm_user_proxy */
uint32_t hlm_user_proxy_create (bdbm_drv_info_t* bdi)
{
	bdbm_hlm_user_proxy_t* p = NULL;

	/* see if hlm_user_proxy is already created */
	if (_hlm_user_proxy == NULL) {
		bdbm_error ("hlm_user_proxy is already created");
		return -EIO;
	}

	/* create bdbm_hlm_user_proxy_t with zeros */
	if ((p = (bdbm_hlm_user_proxy_t*)bdbm_zmalloc (sizeof (bdbm_hlm_user_proxy_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	}

	/* initialize some variables */
	init_waitqueue_head (&p->pollwq);
	bdbm_spin_lock_init (&p->lock);
	bdbm_mutex_init (&p->mutex);
	p->ref_cnt = 0;

	/* assign p to _hlm_user_proxy and bdi */
	bdi->ptr_hlm_inf->ptr_private = (void*)p;
	_hlm_user_proxy = p;

	return 0;
}

void hlm_user_proxy_destroy (bdbm_drv_info_t* bdi)
{
	bdbm_hlm_user_proxy_t* p = (bdbm_hlm_user_proxy_t*)BDBM_HLM_PRIV (bdi);

	/* free all variables related to hlm_user_proxy */
	bdbm_mutex_free (&p->mutex);
	bdbm_spin_lock_destory (&p->lock);
	init_waitqueue_head (&p->pollwq);
	bdbm_free (p);

	_hlm_user_proxy = NULL;
}

uint32_t hlm_user_proxy_make_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* req)
{
	return 0;
}

void hlm_user_proxy_end_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* req)
{
}


/*
 * For the interaction with user-level application
 */
static long hlm_proxy_fops_ioctl (struct file *filp, unsigned int cmd, unsigned long arg);
static unsigned int hlm_proxy_fops_poll (struct file *filp, poll_table *poll_table);
static void mmap_open (struct vm_area_struct *vma);
static void mmap_close (struct vm_area_struct *vma);
static int hlm_proxy_fops_mmap (struct file *filp, struct vm_area_struct *vma);
static int hlm_proxy_fops_create (struct inode *inode, struct file *filp);
static int hlm_proxy_fops_release (struct inode *inode, struct file *filp);

static struct vm_operations_struct mmap_vm_ops = {
	.open = mmap_open,
	.close = mmap_close,
};

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.mmap = hlm_proxy_fops_mmap, 
	.open = hlm_proxy_fops_create,
	.release = hlm_proxy_fops_release,
	.poll = hlm_proxy_fops_poll,
	.unlocked_ioctl = hlm_proxy_fops_ioctl,
	.compat_ioctl = hlm_proxy_fops_ioctl,
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

static int hlm_proxy_fops_mmap (struct file *filp, struct vm_area_struct *vma)
{
	bdbm_hlm_user_proxy_t* s = filp->private_data;
	uint64_t size = vma->vm_end - vma->vm_start;

	if (s == NULL) {
		bdbm_warning ("hlm_proxy is not created yet");
		return -EINVAL;
	}

	if (size > s->mmap_shared_size) {
		bdbm_warning ("size > s->mmap_shared_size: %llu > %llu", size, s->mmap_shared_size);
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_noncached (vma->vm_page_prot);
	vma->vm_pgoff = __pa(s->mmap_shared) >> PAGE_SHIFT;

	if (remap_pfn_range (vma, vma->vm_start, 
			__pa(s->mmap_shared) >> PAGE_SHIFT,
			size, vma->vm_page_prot)) {
		return -EAGAIN;
	}

	vma->vm_ops = &mmap_vm_ops;
	vma->vm_private_data = s;	/* bdbm_hlm_user_proxy_t */
	mmap_open (vma);

	bdbm_msg ("hlm_proxy_fops_mmap is called (%lu)", vma->vm_end - vma->vm_start);

	return 0;
}

static int hlm_proxy_fops_create (struct inode *inode, struct file *filp)
{
#if 0
	bdbm_hlm_user_proxy_t* s = (bdbm_hlm_user_proxy_t*)filp->private_data;

	/* see if bdbm_hlm_user_proxy_ioctl is already created */
	if (s != NULL) {
		bdbm_error ("bdbm_hlm_user_proxy_ioctl is already created; duplicate open is not allowed (s = %p)", s);
		return -EBUSY;
	}

	/* create bdbm_hlm_user_proxy_ioctl with zeros */
	if ((s = (bdbm_hlm_user_proxy_t*)bdbm_zmalloc (sizeof (bdbm_hlm_user_proxy_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	}
	init_waitqueue_head (&s->pollwq);
	bdbm_spin_lock_init (&s->lock);
	s->ref_cnt = 0;

	/* create bdi with zeros */
	if ((s->bdi = (bdbm_drv_info_t*)bdbm_zmalloc 
			(sizeof (bdbm_drv_info_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	} 
	s->bdi->private_data = (void*)s;
	s->bdi->ptr_bdbm_params = &s->params;
	s->bdi->ptr_llm_inf = &_bdbm_llm_inf;	/* register interrupt handler */
	s->bdi->ptr_dm_inf = &_bdbm_dm_inf;	/* register dm handler */

#endif

	// TODO: check # of ref_cnt
	// TODO: register it as a character device

	/* assign bdbm_hlm_user_proxy_ioctl to private_data */
	filp->private_data = (void *)_hlm_user_proxy;
	filp->f_mode |= FMODE_WRITE;

	bdbm_msg ("[%s] The user-level FTL is attached to the kernel succesfully", __FUNCTION__);

	return 0;
}

static int hlm_proxy_fops_release (struct inode *inode, struct file *filp)
{
	bdbm_hlm_user_proxy_t* s = (bdbm_hlm_user_proxy_t*)filp->private_data;

	/* bdbm_hlm_user_proxy_ioctl is not open before */
	if (s == NULL) {
		bdbm_warning ("attempt to close hlm_proxy which was not open");
		return 0;
	}

	/* it is not necessary when hlm_proxy is nicely closed,
	 * bit it is required when a client is crashed */
	/*hlm_proxy_close (s);*/

#if 0
	/* free some variables */
	bdbm_spin_lock_destory (&s->lock);
	init_waitqueue_head (&s->pollwq);
	if (s->bdi != NULL) {
		s->bdi->ptr_dm_inf = NULL;
		bdbm_free (s->bdi);
	}
	bdbm_free (s);
#endif

	/* reset private_data */
	filp->private_data = (void *)NULL;

	bdbm_msg ("hlm_proxy_fops_release is done");

	return 0;
}

static unsigned int hlm_proxy_fops_poll (struct file *filp, poll_table *poll_table)
{
	bdbm_hlm_user_proxy_t* s = (bdbm_hlm_user_proxy_t*)filp->private_data;
	unsigned int mask = 0;

	if (s == NULL) {
		bdbm_error ("bdbm_hlm_user_proxy_ioctl is not created");
		return 0;
	}

	/*bdbm_msg ("hlm_proxy_fops_poll is called");*/

	poll_wait (filp, &s->pollwq, poll_table);

#if 0
	/* TODO: see if there is a rquest that is sent to the user-level FTL */
	if (dm_stub_end_req (s) == 0) {
		mask |= POLLIN | POLLRDNORM; 
	}
#endif

	/*bdbm_msg ("dm_fops_poll is finished");*/

	return mask;
}

static long hlm_proxy_fops_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	bdbm_hlm_user_proxy_t* s = (bdbm_hlm_user_proxy_t*)filp->private_data;
	int ret = 0;

	/* see if hlm_proxy is valid or not */
	if (s == NULL) {
		bdbm_error ("bdbm_hlm_user_proxy_ioctl is not created");
		return -ENOTTY;
	}

	/* handle a command from user applications */
	switch (cmd) {
	case BDBM_HLM_USER_IOCTL_DONE:
		/* TODO: need to call end_req () */
		break;

	default:
		bdbm_warning ("invalid command code");
		ret = -ENOTTY;
	}

	return ret;
}


/*
 * For the registration of hlm_proxy as a character device
 */
static dev_t devnum = 0; 
static struct cdev c_dev;
static struct class *cl = NULL;
static int FIRST_MINOR = 0;
static int MINOR_CNT = 1;

/* register a bdbm_hlm_user_proxy_ioctl driver */
int bdbm_user_proxy_ioctl_init (void)
{
	int ret = -1;
	struct device *dev_ret = NULL;

	if ((ret = alloc_chrdev_region (&devnum, FIRST_MINOR, MINOR_CNT, BDBM_HLM_USER_IOCTL_NAME)) != 0) {
		bdbm_error ("bdbm_hlm_user_proxy_ioctl registration failed: %d\n", ret);
		return ret;
	}
	cdev_init (&c_dev, &fops);

	if ((ret = cdev_add (&c_dev, devnum, MINOR_CNT)) < 0) {
		bdbm_error ("bdbm_hlm_user_proxy_ioctl registration failed: %d\n", ret);
		return ret;
	}

	if (IS_ERR (cl = class_create (THIS_MODULE, "char"))) {
		bdbm_error ("bdbm_hlm_user_proxy_ioctl registration failed: %d\n", MAJOR(devnum));
		cdev_del (&c_dev);
		unregister_chrdev_region (devnum, MINOR_CNT);
		return PTR_ERR (cl);
	}

	if (IS_ERR (dev_ret = device_create (cl, NULL, devnum, NULL, BDBM_HLM_USER_IOCTL_NAME))) {
		bdbm_error ("bdbm_hlm_user_proxy_ioctl registration failed: %d\n", MAJOR(devnum));
		class_destroy (cl);
		cdev_del (&c_dev);
		unregister_chrdev_region (devnum, MINOR_CNT);
		return PTR_ERR (dev_ret);
	}

	bdbm_msg ("bdbm_hlm_user_proxy_ioctl is installed: %s (major:%d minor:%d)", 
		BDBM_HLM_USER_IOCTL_DEVNAME, 
		MAJOR(devnum), MINOR(devnum));

	return 0;
}

/* remove a bdbm_db_stub driver */
void bdbm_user_proxy_ioctl_exit (void)
{
	if (cl == NULL || devnum == 0) {
		bdbm_warning ("bdbm_hlm_user_proxy_ioctl is not installed yet");
		return;
	}

	/* get rid of bdbm_hlm_user_proxy_ioctl */
	device_destroy (cl, devnum);
    class_destroy (cl);
    cdev_del (&c_dev);
    unregister_chrdev_region (devnum, MINOR_CNT);

	bdbm_msg ("bdbm_hlm_user_proxy_ioctl is removed: %s (%d %d)", 
		BDBM_HLM_USER_IOCTL_DEVNAME, 
		MAJOR(devnum), MINOR(devnum));
}

/* NOTE: We create fake interface to avoid compile errors. Do not use them for
 * any other purposes! */
bdbm_ftl_inf_t _ftl_block_ftl, _ftl_page_ftl, _ftl_dftl, _ftl_no_ftl;
bdbm_hlm_inf_t _hlm_dftl_inf, _hlm_buf_inf, _hlm_nobuf_inf, _hlm_rsd_inf;
bdbm_llm_inf_t _llm_mq_inf, _llm_noq_inf;

