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

#if !defined (KERNEL_MODE)
#error "dm_stub only supports KERNEL_MODE"
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/poll.h> /* poll_table, etc. */
#include <linux/cdev.h> /* cdev_init, etc. */
#include <linux/device.h> /* class_create, device_create, etc */
#include <linux/mm.h>  /* mmap related stuff */

#include "bdbm_drv.h"
#include "debug.h"
#include "params.h"
#include "uthread.h"

#include "dm_params.h"
#include "dm_stub.h"
#include "platform.h"


/* exported by the device implementation module */
extern struct bdbm_dm_inf_t _bdbm_dm_inf; 
extern struct bdbm_drv_info* _bdi_dm; 

void __dm_intr_handler (struct bdbm_drv_info* bdi, struct bdbm_llm_req_t* req);

typedef struct {
	wait_queue_head_t pollwq;
	struct bdbm_params params;
	struct bdbm_drv_info* bdi;
	uint64_t punit;
	uint8_t* punit_done;	/* punit_done is updated only in dm_fops_poll while user applications is calling poll () */

	bdbm_spinlock_t lock;
	uint32_t ref_cnt;

	bdbm_spinlock_t lock_busy;
	uint8_t* punit_busy;
	struct bdbm_llm_req_t** kr;
	struct bdbm_llm_req_t** ur;
} bdbm_dm_stub_t;

struct bdbm_llm_inf_t _bdbm_llm_inf = {
	.ptr_private = NULL,
	.create = NULL,
	.destroy = NULL,
	.make_req = NULL,
	.flush = NULL,
	.end_req = __dm_intr_handler, /* 'dm' automatically calls 'end_req' when it gets acks from devices */
};


void __dm_intr_handler (struct bdbm_drv_info* bdi, struct bdbm_llm_req_t* req)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)bdi->private_data;
	uint64_t punit_id = req->phyaddr->channel_no *
		bdi->ptr_bdbm_params->nand.nr_chips_per_channel +
		req->phyaddr->chip_no;
	unsigned long flags;

	bdbm_spin_lock_irqsave (&s->lock_busy, flags);
	if (s->punit_busy[punit_id] != 1) {
		bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);
		/* hmm... this is a serious bug... */
		bdbm_error ("s->punit_busy[punit_id] must be 1 (val = %u)", 
			s->punit_busy[punit_id]);
		bdbm_bug_on (1);
		return;
	}
	s->punit_busy[punit_id] = 2;	/* (2) busy to done */
	bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);

	wake_up_interruptible (&(s->pollwq));
}

/* TODO: We should improve it to use mmap to avoid useless malloc, memcpy, free, etc */
static struct bdbm_llm_req_t* __get_llm_req (struct bdbm_llm_req_t* ur)
{
    int loop = 0;
	struct bdbm_llm_req_t* kr = NULL;
	uint32_t nr_kp_per_fp = 1;

	if (ur == NULL) {
		bdbm_warning ("user-level llm_req is NULL");
		return NULL;
	}

	/* copy a user-level llm_req to a kernel-level llm_req */
    kr = (struct bdbm_llm_req_t*)bdbm_malloc (sizeof (struct bdbm_llm_req_t));
	copy_from_user (kr, ur, sizeof (struct bdbm_llm_req_t));
	
	kr->phyaddr = (struct bdbm_phyaddr_t*)bdbm_malloc (sizeof (struct bdbm_phyaddr_t));
	copy_from_user (kr->phyaddr, ur->phyaddr, sizeof (struct bdbm_phyaddr_t));

	kr->kpg_flags = (uint8_t*)bdbm_malloc (sizeof (uint8_t) * nr_kp_per_fp);
	copy_from_user (kr->kpg_flags, ur->kpg_flags, sizeof (uint8_t) * nr_kp_per_fp);

	kr->pptr_kpgs = (uint8_t**)bdbm_malloc (sizeof (uint8_t*) * nr_kp_per_fp);
	for (loop = 0; loop < nr_kp_per_fp; loop++) {
        kr->pptr_kpgs[loop] = (uint8_t*)get_zeroed_page (GFP_KERNEL);

		/* copy user-data to Kernel only if the type of request is a write */
		/* FIXME: maybe, we need to handle RMW in a different way */
		if (kr->req_type == REQTYPE_WRITE ||
			kr->req_type == REQTYPE_RMW_WRITE ||
			kr->req_type == REQTYPE_GC_WRITE) {
			copy_from_user (
				kr->pptr_kpgs[loop], 
				ur->pptr_kpgs[loop], 
				KERNEL_PAGE_SIZE);
		}
	}
	
	kr->ptr_oob = (uint8_t*)bdbm_malloc (sizeof (uint8_t) * 64);
	copy_from_user (kr->ptr_oob, ur->ptr_oob, 64);
	kr->ret = 1;

#if 0
	/* display some values */
	bdbm_msg ("req_type: %u", 
		kr->req_type);
	bdbm_msg ("lpa: %llu", 
		kr->lpa);
	bdbm_msg ("phyaddr: %llu %llu %llu %llu", 
		kr->phyaddr->channel_no, 
		kr->phyaddr->chip_no,
		kr->phyaddr->block_no,
		kr->phyaddr->page_no);
	bdbm_msg ("kpg_flags: %u", 
		kr->kpg_flags[0]);
	bdbm_msg ("data: %X %X %X ...",
		kr->pptr_kpgs[0][0],
		kr->pptr_kpgs[0][1],
		kr->pptr_kpgs[0][2]);
#endif

    return kr;
}

static void __return_llm_req (
	struct bdbm_llm_req_t* ur,
	struct bdbm_llm_req_t* kr)
{
	/* copy a retun value */
	copy_to_user (&ur->ret, &kr->ret, sizeof (uint8_t));

	/* copy data in Kernel to user-space if it is a read request, 
	 * except for REQTYPE_READ_DUMMY */
	if (kr->req_type == REQTYPE_READ ||
		kr->req_type == REQTYPE_RMW_READ ||
		kr->req_type == REQTYPE_GC_READ) {
		uint32_t nr_kp_per_fp = 1;
		uint32_t loop = 0;
		for (loop = 0; loop < nr_kp_per_fp; loop++) {
			copy_to_user (
				ur->pptr_kpgs[loop], 
				kr->pptr_kpgs[loop], 
				KERNEL_PAGE_SIZE);
		}
	}
}

static void __free_llm_req (struct bdbm_llm_req_t* kr)
{
    int loop = 0;
	uint32_t nr_kp_per_fp = 1;

	bdbm_free (kr->phyaddr);
	bdbm_free (kr->kpg_flags);
	for (loop = 0; loop < nr_kp_per_fp; loop++)
		free_page ((unsigned long)kr->pptr_kpgs[loop]);
	bdbm_free (kr->pptr_kpgs);
	bdbm_free (kr->ptr_oob);
	bdbm_free (kr);
}

static int dm_stub_probe (bdbm_dm_stub_t* s)
{
	struct bdbm_drv_info* bdi = s->bdi;

	if (bdi->ptr_dm_inf->probe == NULL) {
		bdbm_warning ("ptr_dm_inf->probe is NULL");
		return -ENOTTY;
	}

	/* call probe */
	if (bdi->ptr_dm_inf->probe (bdi, &bdi->ptr_bdbm_params->nand) != 0) {
		bdbm_warning ("dm->probe () failed");
		return -EIO;
	} 

	return 0;
}

static int dm_stub_open (bdbm_dm_stub_t* s)
{
	struct bdbm_drv_info* bdi = s->bdi;
	unsigned long flags;

	if (bdi->ptr_dm_inf->open == NULL) {
		bdbm_warning ("ptr_dm_inf->open is NULL");
		return -ENOTTY;
	}

	/* a big spin-lock; but not a problem with open */
	bdbm_spin_lock (&s->lock);
	bdbm_spin_lock_irqsave (&s->lock_busy, flags);
	{
		/* are there any other clients? */
		if (s->ref_cnt > 0) {
			bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);
			bdbm_spin_unlock (&s->lock);
			bdbm_warning ("dm_stub is already open for other clients (%u)", s->ref_cnt);
			return -EBUSY;
		} 

		/* initialize internal variables */
		s->punit = s->params.nand.nr_chips_per_channel * s->params.nand.nr_channels;
		s->punit_busy = (uint8_t*)bdbm_malloc_atomic (s->punit * sizeof (uint8_t));
		s->kr = (struct bdbm_llm_req_t**)bdbm_zmalloc (s->punit * sizeof (struct bdbm_llm_req_t*));
		s->ur = (struct bdbm_llm_req_t**)bdbm_zmalloc (s->punit * sizeof (struct bdbm_llm_req_t*));
		s->punit_done = (uint8_t*)get_zeroed_page (GFP_KERNEL); /* get with zeros */

		/* are there any errors? */
		if (s->kr == NULL || 
			s->ur == NULL || 
			s->punit_busy == NULL || 
			s->punit_done == NULL) {

			if (s->kr) bdbm_free (s->kr);
			if (s->ur) bdbm_free (s->ur);
			if (s->punit_busy) bdbm_free_atomic (s->punit_busy);
			if (s->punit_done) free_page ((unsigned long)s->punit_done);

			s->kr = NULL;
			s->ur = NULL;
			s->punit_busy = NULL;
			s->punit_done = NULL;

			bdbm_warning ("bdbm_malloc failed (kr=%p, ur=%p, punit_busy=%p, punit_done=%p)", 
					s->kr, s->ur, s->punit_busy, s->punit_done);

			bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);
			bdbm_spin_unlock (&s->lock);
			return -EIO;
		}

		/* increase ref_cnt */
		s->ref_cnt = 1;
	}
	bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);
	bdbm_spin_unlock (&s->lock);

	/* call open */
	if (bdi->ptr_dm_inf->open (bdi) != 0) {
		bdbm_warning ("dm->open () failed");
		return -EIO;
	}

	return 0;
}

static int dm_stub_close (bdbm_dm_stub_t* s)
{
	struct bdbm_drv_info* bdi = s->bdi;

	if (bdi->ptr_dm_inf->close == NULL) {
		bdbm_warning ("ptr_dm_inf->close is NULL");
		return -ENOTTY;
	}

	bdbm_spin_lock (&s->lock);
	if (s->ref_cnt == 0) {
		bdbm_warning ("oops! bdbm_dm_stub is not open");
		bdbm_spin_unlock (&s->lock);
		return -ENOTTY;
	}
	s->ref_cnt = 0;
	bdbm_spin_unlock (&s->lock);

	/* call close */
	bdi->ptr_dm_inf->close (bdi);

	/* free llm_reqs for bdi */
	if (s->punit_done) free_page ((unsigned long)s->punit_done);
	if (s->punit_busy) bdbm_free_atomic (s->punit_busy);
	if (s->kr) bdbm_free (s->kr);
	if (s->ur) bdbm_free (s->ur);

	s->punit_done = NULL;
	s->punit_busy = NULL;
	s->kr = NULL;
	s->ur = NULL;

	return 0;
}

static int dm_stub_make_req (bdbm_dm_stub_t* s, struct bdbm_llm_req_t* ur)
{
	struct bdbm_drv_info* bdi = s->bdi;
	struct bdbm_llm_req_t* kr = NULL;
	uint32_t punit_id = -1;
	unsigned long flags;

	if (bdi->ptr_dm_inf->make_req == NULL) {
		bdbm_warning ("ptr_dm_inf->make_req is NULL");
		return -ENOTTY;
	} 

	/* the current implementation of bdbm_dm stub only supports a single client */
	bdbm_spin_lock (&s->lock);
	if (s->ref_cnt == 0) {
		bdbm_warning ("oops! bdbm_dm_stub is not open");
		bdbm_spin_unlock (&s->lock);
		return -EBUSY;
	}
	bdbm_spin_unlock (&s->lock);

	/* copy user-level llm_req to kernel-level */
	if ((kr = __get_llm_req (ur)) == NULL) {
		bdbm_warning ("__get_llm_req () failed (ur=%p, kr=%p)", ur, kr);
		return -EIO;
	}

	/* get punit_id */
	punit_id = kr->phyaddr->channel_no *
		s->params.nand.nr_chips_per_channel +
		kr->phyaddr->chip_no;

	/* see if there is an on-going request */
	bdbm_spin_lock_irqsave (&s->lock_busy, flags);
	if (s->punit_busy[punit_id] != 0) {
		bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);
		bdbm_warning ("oops! the punit for the request is busy (punit_id = %u)", punit_id);
		__free_llm_req (kr);
		return -EBUSY;
	}
	s->punit_busy[punit_id] = 1; /* (1) idle to busy */
	s->ur[punit_id] = ur;
	s->kr[punit_id] = kr;
	bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);

	/* call make_req */ 
	if (bdi->ptr_dm_inf->make_req (bdi, kr) != 0) {
		return -EIO;
	}

	return 0;
}

static int dm_stub_end_req (bdbm_dm_stub_t* s)
{
	struct bdbm_llm_req_t* kr = NULL;
	struct bdbm_llm_req_t* ur = NULL;
	uint64_t loop;
	int ret = 1;

	/* see if there are available punits */
	for (loop = 0; loop < s->punit; loop++) {
		unsigned long flags;

		/* see if there is a request that ends */
		bdbm_spin_lock_irqsave (&s->lock_busy, flags);
		if (s->punit_busy[loop] != 2) {
			bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);
			continue;
		}
		kr = s->kr[loop];
		ur = s->ur[loop];
		s->kr[loop] = NULL;
		s->ur[loop] = NULL;
		s->punit_busy[loop] = 0;	/* (3) done to idle */
		bdbm_spin_unlock_irqrestore (&s->lock_busy, flags);
		
		/* let's finish it */
		if (ur != NULL && kr != NULL) {
			/* setup results and destroy a kernel copy */
			__return_llm_req (ur, kr);
			__free_llm_req (kr);

			/* done */
			s->punit_done[loop] = 1; /* don't need to use a lock for this */
			ret = 0;
		} else {
			bdbm_error ("hmm... this is impossible");
		}
	}

	return ret;
}


/*
 * For the interaction with user-level application
 */
static long dm_fops_ioctl (struct file *filp, unsigned int cmd, unsigned long arg);
static unsigned int dm_fops_poll (struct file *filp, poll_table *poll_table);
static int mmap_fault (struct vm_area_struct *vma, struct vm_fault *vmf);
static int dm_fops_mmap (struct file *filp, struct vm_area_struct *vma);
static int dm_fops_create (struct inode *inode, struct file *filp);
static int dm_fops_release (struct inode *inode, struct file *filp);

/* http://stackoverflow.com/questions/10760479/mmap-kernel-buffer-to-user-space */
struct vm_operations_struct mmap_vm_ops = {
	.fault = mmap_fault,
};

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.mmap = dm_fops_mmap, /* TODO: implement mmap later to avoid usless operations */
	.open = dm_fops_create,
	.release = dm_fops_release,
	.poll = dm_fops_poll,
	.unlocked_ioctl = dm_fops_ioctl,
	.compat_ioctl = dm_fops_ioctl,
};

static int mmap_fault (struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page = NULL;
	bdbm_dm_stub_t* s = NULL;

	bdbm_msg ("mmap_fault is called");

	/* see if s and s->punit_done is available */
	s = (bdbm_dm_stub_t *)vma->vm_private_data;
	if (s == NULL || !s->punit_done) {
		bdbm_warning ("s or punit_done is not allocated yet");
		return -ENOMEM;
	}

	/* get the page */
	page = virt_to_page (s->punit_done);
	
	/* increment the reference count of this page */
	get_page (page);
	vmf->page = page;

	return 0;
}

static int dm_fops_mmap (struct file *filp, struct vm_area_struct *vma)
{
	/* TODO: not implemented yet */
	bdbm_dm_stub_t* s = filp->private_data;

	if (s == NULL) {
		bdbm_warning ("dm_stub is not created yet");
		return 1;
	}

	vma->vm_ops = &mmap_vm_ops;
	vma->vm_private_data = filp->private_data;	/* bdbm_dm_stub_t */

	bdbm_msg ("dm_fops_mmap is called");

	return 0;
}

static int dm_fops_create (struct inode *inode, struct file *filp)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)filp->private_data;

	/* see if bdbm_dm is already used by other applications */
	if (_bdi_dm != NULL) {
		bdbm_error ("bdbm_dm is already used by other applications (_bdi_dm = %p)", _bdi_dm);
		return -EBUSY;
	}

	/* see if bdbm_dm_stub is already created */
	if (s != NULL) {
		bdbm_error ("bdbm_dm_stub is already created; duplicate open is not allowed (s = %p)", s);
		return -EBUSY;
	}

	/* create bdbm_dm_stub with zeros */
	if ((s = (bdbm_dm_stub_t*)bdbm_zmalloc (sizeof (bdbm_dm_stub_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	}
	init_waitqueue_head (&s->pollwq);
	bdbm_spin_lock_init (&s->lock);
	bdbm_spin_lock_init (&s->lock_busy);
	s->ref_cnt = 0;

	/* create bdi with zeros */
	if ((s->bdi = (struct bdbm_drv_info*)bdbm_zmalloc 
			(sizeof (struct bdbm_drv_info))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	} 
	s->bdi->private_data = (void*)s;
	s->bdi->ptr_bdbm_params = &s->params;
	s->bdi->ptr_llm_inf = &_bdbm_llm_inf;	/* register interrupt handler */
	s->bdi->ptr_dm_inf = &_bdbm_dm_inf;	/* register dm handler */

	/* assign bdbm_dm_stub to private_data */
	filp->private_data = (void *)s;
	filp->f_mode |= FMODE_WRITE;

	_bdi_dm = s->bdi;

	bdbm_msg ("dm_fops_create is done");

	return 0;
}

static int dm_fops_release (struct inode *inode, struct file *filp)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)filp->private_data;

	/* bdbm_dm_stub is not open before */
	if (s == NULL) {
		bdbm_warning ("attempt to close dm_stub which was not open");
		return 0;
	}

	/* free some variables */
	bdbm_spin_lock_destory (&s->lock_busy);
	bdbm_spin_lock_destory (&s->lock);
	init_waitqueue_head (&s->pollwq);
	if (s->bdi != NULL) {
		s->bdi->ptr_dm_inf = NULL;
		bdbm_free (s->bdi);
	}
	bdbm_free (s);

	/* reset private_data */
	filp->private_data = (void *)NULL;
	_bdi_dm = NULL;

	bdbm_msg ("dm_fops_release is done");

	return 0;
}

static unsigned int dm_fops_poll (struct file *filp, poll_table *poll_table)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)filp->private_data;
	unsigned int mask = 0;

	if (s == NULL) {
		bdbm_error ("bdbm_dm_stub is not created");
		return 0;
	}

	poll_wait (filp, &s->pollwq, poll_table);

	/* see if there are requests that already finished */
	if (dm_stub_end_req (s) == 0) {
		mask |= POLLIN | POLLRDNORM; 
	}

	return mask;
}

static long dm_fops_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)filp->private_data;
	int ret = 0;

	/* see if bdbm_dm_study is valid or not */
	if (s == NULL) {
		bdbm_error ("bdbm_dm_stub is not created");
		return -ENOTTY;
	}

	/* handle a command from user applications */
	switch (cmd) {
	case BDBM_DM_IOCTL_PROBE:
		if ((ret = dm_stub_probe (s)) == 0)
			copy_to_user ((struct nand_params*)arg, &s->params.nand, sizeof (struct nand_params));
		break;
	case BDBM_DM_IOCTL_OPEN:
		ret = dm_stub_open (s);
		break;
	case BDBM_DM_IOCTL_CLOSE:
		ret = dm_stub_close (s);
		break;
	case BDBM_DM_IOCTL_MAKE_REQ:
		ret = dm_stub_make_req (s, (struct bdbm_llm_req_t*)arg);
		break;
	case BDBM_DM_IOCTL_END_REQ:
		bdbm_warning ("Hmm... dm_stub_end_req () cannot be directly called by user applications");
		ret = -ENOTTY;
		break;
	case BDBM_DM_IOCTL_LOAD:
		bdbm_warning ("dm_stub_load () is not implemented yet");
		ret = -ENOTTY;
		break;
	case BDBM_DM_IOCTL_STORE:
		bdbm_warning ("dm_stub_store () is not implemented yet");
		ret = -ENOTTY;
		break;
	default:
		bdbm_warning ("invalid command code");
		ret = -ENOTTY;
	}

	return ret;
}


/*
 * For the registration of dm_stub as a character device
 */
static dev_t devnum = 0; 
static struct cdev c_dev;
static struct class *cl = NULL;
static int FIRST_MINOR = 0;
static int MINOR_CNT = 1;

/* register a bdbm_dm_stub driver */
int bdbm_dm_stub_init (void)
{
	int ret = -1;
	struct device *dev_ret = NULL;

	if ((ret = alloc_chrdev_region (&devnum, FIRST_MINOR, MINOR_CNT, BDBM_DM_IOCTL_NAME)) != 0) {
		bdbm_error ("bdbm_dm_stub registration failed: %d\n", ret);
		return ret;
	}
	cdev_init (&c_dev, &fops);

	if ((ret = cdev_add (&c_dev, devnum, MINOR_CNT)) < 0) {
		bdbm_error ("bdbm_dm_stub registration failed: %d\n", ret);
		return ret;
	}

	if (IS_ERR (cl = class_create (THIS_MODULE, "char"))) {
		bdbm_error ("bdbm_dm_stub registration failed: %d\n", MAJOR(devnum));
		cdev_del (&c_dev);
		unregister_chrdev_region (devnum, MINOR_CNT);
		return PTR_ERR (cl);
	}

	if (IS_ERR (dev_ret = device_create (cl, NULL, devnum, NULL, BDBM_DM_IOCTL_NAME))) {
		bdbm_error ("bdbm_dm_stub registration failed: %d\n", MAJOR(devnum));
		class_destroy (cl);
		cdev_del (&c_dev);
		unregister_chrdev_region (devnum, MINOR_CNT);
		return PTR_ERR (dev_ret);
	}

	bdbm_msg ("bdbm_dm_stub is installed: %s (major:%d minor:%d)", 
		BDBM_DM_IOCTL_DEVNAME, 
		MAJOR(devnum), MINOR(devnum));

	return 0;
}

/* remove a bdbm_db_stub driver */
void bdbm_dm_stub_exit (void)
{
	if (cl == NULL || devnum == 0) {
		bdbm_warning ("bdbm_dm_stub is not installed yet");
		return;
	}

	/* get rid of bdbm_dm_stub */
	device_destroy (cl, devnum);
    class_destroy (cl);
    cdev_del (&c_dev);
    unregister_chrdev_region (devnum, MINOR_CNT);

	bdbm_msg ("bdbm_dm_stub is removed: %s (%d %d)", 
		BDBM_DM_IOCTL_DEVNAME, 
		MAJOR(devnum), MINOR(devnum));
}

