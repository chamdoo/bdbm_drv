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


extern struct bdbm_dm_inf_t _bdbm_dm_inf; /* exported by the device implementation module */
extern struct bdbm_drv_info* _bdi_dm; 

typedef struct {
	wait_queue_head_t pollwq;
	struct bdbm_llm_req_t** kr;
	struct bdbm_llm_req_t** ur;
	uint8_t* punit_done;
	uint8_t* punit_busy;
} bdbm_dm_stub_t;

void llm_end_req (struct bdbm_drv_info* bdi, struct bdbm_llm_req_t* req)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)bdi->private_data;
	uint64_t punit_id = req->phyaddr->channel_no *
		bdi->ptr_bdbm_params->nand.nr_chips_per_channel +
		req->phyaddr->chip_no;

	bdbm_msg ("llm_end_req is invoked");

	msleep (1000);

	req->ret = 33;
	s->punit_busy[punit_id] = 0;

	wake_up_interruptible (&(s->pollwq));
}

struct bdbm_llm_inf_t _bdbm_llm_inf = {
	.ptr_private = NULL,
	.create = NULL,
	.destroy = NULL,
	.make_req = NULL,
	.flush = NULL,
	.end_req = llm_end_req,
};

static int dm_stub_open (struct inode *inode, struct file *filp)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)filp->private_data;

	/* see if bdbm_dm is already used by other applications */
	if (_bdi_dm != NULL) {
		bdbm_error ("bdbm_dm is already used by other applications");
		return -EBUSY;
	}

	/* see if bdbm_dm_stub is already created */
	if (s != NULL) {
		bdbm_error ("bdbm_dm_stub is already created");
		return -EIO;
	}

	/* create bdbm_dm_stub */
	if ((s = (bdbm_dm_stub_t*)bdbm_malloc (sizeof (bdbm_dm_stub_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	}
	init_waitqueue_head (&s->pollwq);
	s->kr = NULL;	/* will be initialized when probe () is called */
	s->ur = NULL;
	s->punit_busy = NULL;
	s->punit_done = NULL;

	/* create bdi */
	if ((_bdi_dm = (struct bdbm_drv_info*)bdbm_malloc 
			(sizeof (struct bdbm_drv_info))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	} 
	_bdi_dm->private_data = (void*)s;
	_bdi_dm->ptr_host_inf = NULL;
	_bdi_dm->ptr_hlm_inf = NULL;
	_bdi_dm->ptr_llm_inf = &_bdbm_llm_inf;
	_bdi_dm->ptr_ftl_inf = NULL;
	_bdi_dm->ptr_bdbm_params = (struct bdbm_params*)bdbm_malloc (sizeof (struct bdbm_params));
	_bdi_dm->ptr_dm_inf = &_bdbm_dm_inf;

	/* assign bdbm_dm_stub to private_data */
	filp->private_data = (void *)s;
	filp->f_mode |= FMODE_WRITE;

	bdbm_msg ("dm_stub_open is done");

	return 0;
}

static int dm_stub_release (struct inode *inode, struct file *filp)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)filp->private_data;

	/* bdbm_dm_stub is not open before */
	if (s == NULL)
		return 0;

	/* free some variables */
	init_waitqueue_head (&s->pollwq);
	if (_bdi_dm != NULL) {
		_bdi_dm->ptr_dm_inf = NULL;
		bdbm_free (_bdi_dm->ptr_bdbm_params);
		bdbm_free (_bdi_dm);
		_bdi_dm = NULL;
	}
	bdbm_free (s);

	/* reset private_data */
	filp->private_data = (void *)NULL;

	bdbm_msg ("dm_stub_release is done");

	return 0;
}

/* TODO: should improve __get_llm_req with mmap to avoid useless malloc, memcpy, free, etc */
static struct bdbm_llm_req_t* __get_llm_req (
	struct bdbm_llm_req_t* ur)
{
    int loop = 0;
	struct bdbm_llm_req_t* kr = NULL;
	uint32_t nr_kp_per_fp = 1;

	if (ur == NULL) {
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
		copy_from_user (kr->pptr_kpgs[loop], ur->pptr_kpgs[loop], KERNEL_PAGE_SIZE);
	}
	
	kr->ptr_oob = (uint8_t*)bdbm_malloc (sizeof (uint8_t) * 64);
	copy_from_user (kr->ptr_oob, ur->ptr_oob, 64);

	kr->ret = 1;

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

    return kr;
}

static void __return_llm_req (
	struct bdbm_llm_req_t* ur,
	struct bdbm_llm_req_t* kr)
{
	copy_to_user (&ur->ret, &kr->ret, sizeof (uint8_t));
}

static void __release_llm_req (struct bdbm_llm_req_t* kr)
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

static long dm_stub_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)filp->private_data;

	/* see if bdbm_dm_study is valid or not */
	if (s == NULL) {
		bdbm_error ("bdbm_dm_stub is not created");
		return -ENOTTY;
	}

	/* handle a command from user applications */
	switch (cmd) {
	case BDBM_DM_IOCTL_PROBE:
		if (_bdi_dm == NULL || _bdi_dm->ptr_dm_inf->probe == NULL) {
			bdbm_warning ("_bdi_dm or _bdi_dm->ptr_dm_inf->probe is NULL");
			return -ENOTTY;
		}

		/* call probe */
		if (_bdi_dm->ptr_dm_inf->probe (_bdi_dm, &_bdi_dm->ptr_bdbm_params->nand) != 0) {
			bdbm_warning ("dm->probe () failed");
			return -ENOTTY;
		} else {
			uint64_t punit = 
				_bdi_dm->ptr_bdbm_params->nand.nr_channels *
				_bdi_dm->ptr_bdbm_params->nand.nr_chips_per_channel;

			/* create llm_reqs for bdi */
			s->kr = (struct bdbm_llm_req_t**)bdbm_zmalloc (punit * sizeof (struct bdbm_llm_req_t*));
			s->ur = (struct bdbm_llm_req_t**)bdbm_zmalloc (punit * sizeof (struct bdbm_llm_req_t*));
			s->punit_busy = (uint8_t*)bdbm_malloc_atomic (punit * sizeof (uint8_t));
			s->punit_done = (uint8_t*)get_zeroed_page (GFP_KERNEL); /* get with zeros */

			if (s->kr == NULL || 
				s->ur == NULL || 
				s->punit_busy == NULL ||
				s->punit_done == NULL) {
				bdbm_warning ("bdbm_malloc failed (kr=%p, ur=%p, punit_busy=%p, punit_done=%p)", 
					s->kr, s->ur, s->punit_busy, s->punit_done);
				return -ENOTTY;
			}
		}

		/* copy nand params to user-space */
		copy_to_user ((struct nand_params*)arg, &_bdi_dm->ptr_bdbm_params->nand, sizeof (struct nand_params));
		break;

	case BDBM_DM_IOCTL_OPEN:
		if (_bdi_dm == NULL || _bdi_dm->ptr_dm_inf->open == NULL) {
			bdbm_warning ("_bdi_dm or _bdi_dm->ptr_dm_inf->open is NULL");
			return -ENOTTY;
		}

		/* call open */
		if (_bdi_dm->ptr_dm_inf->open (_bdi_dm) != 0) {
			bdbm_warning ("dm->open () failed");
			return -ENOTTY;
		}
		break;

	case BDBM_DM_IOCTL_CLOSE:
		if (_bdi_dm == NULL || _bdi_dm->ptr_dm_inf->close == NULL) {
			bdbm_warning ("_bdi_dm or _bdi_dm->ptr_dm_inf->close is NULL");
			return -ENOTTY;
		}

		/* call close */
		_bdi_dm->ptr_dm_inf->close (_bdi_dm);

		/* free llm_reqs for bdi */
		if (s->punit_done) {
			bdbm_msg ("punit_done[0] = %u", s->punit_done[0]);
			free_page ((unsigned long)s->punit_done);
		}
		if (s->punit_busy) bdbm_free_atomic (s->punit_busy);
		if (s->kr) bdbm_free (s->kr);
		if (s->ur) bdbm_free (s->ur);
		s->punit_done = NULL;
		s->punit_busy = NULL;
		s->kr = NULL;
		s->ur = NULL;
		break;

	case BDBM_DM_IOCTL_MAKE_REQ:
		if (_bdi_dm == NULL || _bdi_dm->ptr_dm_inf->make_req == NULL) {
			bdbm_warning ("_bdi_dm or _bdi_dm->ptr_dm_inf->make_req is NULL");
			return -ENOTTY;
		} 

		if (s->ur != NULL && s->kr != NULL) {
			uint32_t punit_id = -1;
			struct bdbm_llm_req_t* ur = (struct bdbm_llm_req_t*)arg;
			struct bdbm_llm_req_t* kr = __get_llm_req (ur);

			/* copy llm_req to kernel */
			if (kr == NULL) {
				bdbm_warning ("__get_llm_req () failed (ur=%p, kr=%p)", ur, kr);
				return -ENOTTY;
			}

			/* get punit_id */
			punit_id = kr->phyaddr->channel_no *
				_bdi_dm->ptr_bdbm_params->nand.nr_chips_per_channel +
				kr->phyaddr->chip_no;

			/* see if there is a request to the same punit */
			if (s->punit_busy[punit_id] != 0) {
				bdbm_warning ("oops! the punit for the request is busy (punit_id = %u)", punit_id);
				return -EBUSY;
			}

			s->punit_busy[punit_id] = 1;
			s->ur[punit_id] = ur;
			s->kr[punit_id] = kr;

			/* call make_req */ 
			if (_bdi_dm->ptr_dm_inf->make_req (_bdi_dm, kr) != 0) {
				return -ENOTTY;
			}
		}
		break;

	case BDBM_DM_IOCTL_END_REQ:
		bdbm_warning ("Hmm... dm_stub_end_req () cannot be directly called by user applications");
		return -ENOTTY;

	case BDBM_DM_IOCTL_LOAD:
		bdbm_warning ("dm_stub_load () is not implemented yet");
		return -ENOTTY;

	case BDBM_DM_IOCTL_STORE:
		bdbm_warning ("dm_stub_store () is not implemented yet");
		return -ENOTTY;

	default:
		bdbm_warning ("invalid command code");
		return -ENOTTY;
	}

	return 0;
}

static unsigned int dm_stub_poll (struct file *filp, poll_table *poll_table)
{
	bdbm_dm_stub_t* s = (bdbm_dm_stub_t*)filp->private_data;
	unsigned int mask = 0;
	uint64_t loop, punit;

	bdbm_msg ("dm_stub_poll is invoked");

	poll_wait (filp, &s->pollwq, poll_table);

	/* get # of punits in flash */
	punit = _bdi_dm->ptr_bdbm_params->nand.nr_channels *
		_bdi_dm->ptr_bdbm_params->nand.nr_chips_per_channel;

	/* see if there are available punits */
	for (loop = 0; loop < punit; loop++) {
		if (s->punit_busy[loop] == 0) {
			if (s->ur[loop] != NULL && s->kr[loop] != NULL) {
				/* OK... there are available punits */
				__return_llm_req (s->ur[loop], s->kr[loop]);
				__release_llm_req (s->kr[loop]);

				s->ur[loop] = NULL;
				s->kr[loop] = NULL;

				mask |= POLLIN | POLLRDNORM; 

				if (s->punit_done) {
					s->punit_done[loop] = 1; /* done */
				}

				bdbm_msg ("poll_wait done");
			}
		}
	}

	return mask;
}

static int mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
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
	vmf->page = page;					//--changed

	return 0;
}

/* http://stackoverflow.com/questions/10760479/mmap-kernel-buffer-to-user-space */
struct vm_operations_struct mmap_vm_ops = {
	.fault = mmap_fault,
};

static int dm_stub_mmap (struct file *filp, struct vm_area_struct *vma)
{
	/* TODO: not implemented yet */
	bdbm_dm_stub_t* s = filp->private_data;

	bdbm_msg ("dm_stub_mmap is called");

	if (s == NULL) {
		bdbm_warning ("dm_stub is not created yet");
		return 1;
	}

	vma->vm_ops = &mmap_vm_ops;
	vma->vm_private_data = filp->private_data;	/* bdbm_dm_stub_t */

	return 0;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = dm_stub_open,
	.release = dm_stub_release,
	.unlocked_ioctl = dm_stub_ioctl,
	.compat_ioctl = dm_stub_ioctl,
	.poll = dm_stub_poll,
	.mmap = dm_stub_mmap, /* TODO: implement mmap later to avoid usless operations */
};


/* ===================================================
 *
 * register a character device driver for bdbm_dm_stub
 *
 * =================================================== */
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

