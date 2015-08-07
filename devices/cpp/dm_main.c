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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/poll.h> /* poll_table, etc. */
#include <linux/interrupt.h> /* tasklet */
#include <linux/cdev.h> /* cdev_init, etc. */
#include <linux/device.h> /* class_create, device_create, etc */
#include <linux/workqueue.h> /* workqueue */
/*#include <linux/slab.h> *//* get_zeroed_page */

#elif defined (USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "bdbm_drv.h"
#include "debug.h"
#include "params.h"
#include "dm_params.h"
#include "dm_ioctl.h"
#include "uthread.h"

/* It must be exported by the device implementation module */
extern struct bdbm_dm_inf_t _bdbm_dm_inf; 

/* It is used by the device implementation module */
struct bdbm_drv_info* _bdi_dm = NULL;


#if defined (KERNEL_MODE)

#define FIRST_MINOR	0
#define MINOR_CNT	1

typedef struct {
	wait_queue_head_t wait_queue;
	dev_t devnum; 
	struct tasklet_struct* tasklet;	/* for tasklet */
	struct workqueue_struct* workqueue; /* for workqueue */
} bdbm_dm_t;

typedef struct {
	struct delayed_work* work;
	int id;
	wait_queue_head_t wq;
} bdbm_dm_work_t;

bdbm_dm_t* _bdm = NULL;

static int done = 0;

static void __tasklet_handler_done (unsigned long arg)
{
	bdbm_dm_t* b = (bdbm_dm_t*)arg;

	bdbm_msg ("__tasklet_handler_done is invoked");

	/*bdbm_thread_msleep (1000);*/
	mdelay (5000);
	done = 1;

	bdbm_msg ("__tasklet_handler_done is done");

	wake_up_interruptible (&(b->wait_queue));
}

#if 0
static void __work_done ( struct delayed_work *work)
{
	bdbm_dm_work_t *current_work = (bdbm_dm_work_t *)work;

	bdbm_msg ("work id = %d\n", current_work->id);

	wake_up_interruptible (&(current_work->wq));

	return;
}
#endif

static int dm_open(struct inode *inode, struct file *filp)
{
	int err = 0;

	bdbm_msg ("dm_open");

	filp->private_data = (void *)_bdm;
	filp->f_mode |= FMODE_WRITE;

	return err;
}

static int dm_release (struct inode *inode, struct file *filp)
{
	bdbm_msg ("dm_release");

	return 0;
}

/* TODO: should improve __get_llm_req with mmap to avoid useless malloc, memcpy, free, etc */
static struct bdbm_llm_req_t* __get_llm_req (
	struct bdbm_llm_req_t* ur)
{
    int loop = 0;
	struct bdbm_llm_req_t* kr = NULL;
	uint32_t nr_kp_per_fp = 1;

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
	struct bdbm_llm_req_t* req_user,
	struct bdbm_llm_req_t* kr)
{
	copy_to_user (&req_user->ret, &kr->ret, sizeof (uint8_t));
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

struct bdbm_llm_req_t* _kr = NULL;
struct bdbm_llm_req_t* ur = NULL;

static long dm_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case BDBM_DM_IOCTL_MAKE_REQ:
		if (_bdm != NULL && _bdm->tasklet != NULL) {
			struct bdbm_llm_req_t* req_user = (struct bdbm_llm_req_t*)arg;
			struct bdbm_llm_req_t* kr = NULL;
			
			bdbm_msg ("get a command from user-app");
			done = 0;

			kr = __get_llm_req (req_user);

			_kr = kr;
			ur = req_user;

			tasklet_schedule (_bdm->tasklet);

		} else {
			bdbm_warning ("Either _bdm or _bdm_tasklet is NULL");
		}

#if 0
		if (_bdm != NULL && _bdm->workqueue != NULL) {
			bdbm_dm_work_t* work = NULL;
			if ((work = (bdbm_dm_work_t*)bdbm_malloc_atomic (sizeof (bdbm_dm_work_t))) == NULL) {
				bdbm_warning ("bdbm_malloc failed");
				return -ENOTTY;
			}

			INIT_DELAYED_WORK ((struct delayed_work*)work, __work_done);
			work->id = global_id++;
			init_waitqueue_head (&work->wq);
			if ((ret = queue_delayed_work (_bdm->workqueue, (struct delayed_work*)work, msecs_to_jiffies(1000))) != 0) {
				bdbm_warning ("queue_work failed (ret = %d)", ret);
				return -ENOTTY;
			}

			interruptible_sleep_on (&work->wq);
			bdbm_free_atomic (work);
		}
#endif
		break;
	default:
		bdbm_warning ("invalid command code");
		return -ENOTTY;
	}

	return 0;
}

static unsigned int dm_poll (
	struct file *filp, 
	poll_table *poll_table)
{
	unsigned int mask = 0;
	bdbm_dm_t* bdm = (bdbm_dm_t*)filp->private_data;

	poll_wait (filp, &bdm->wait_queue, poll_table);

	if (done == 1) {
		bdbm_msg ("poll_wait done");
		mask |= POLLIN | POLLRDNORM; /* readable */

		if (_kr != NULL) {
			bdbm_msg ("release llm_req");
			__return_llm_req (ur, _kr);
			__release_llm_req (_kr);
			_kr = NULL;
		}
	}

	return mask;
}

static int dm_mmap (
	struct file *filp, 
	struct vm_area_struct *vma)
{
	/* TODO: not implemented yet */
	return 0;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = dm_open,
	.release = dm_release,
	.unlocked_ioctl = dm_ioctl,
	.compat_ioctl = dm_ioctl,
	.poll = dm_poll,
	.mmap = dm_mmap, /* TODO: implement it later to avoid data copy */
};

#endif


static dev_t dev;
static struct cdev c_dev;
static struct class *cl;


#if defined (KERNEL_MODE)
static int __init risa_dev_init (void)
#elif defined (USER_MODE)
int risa_dev_init (void)
#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif
{
#if defined (KERNEL_MODE)
	int ret = -1;
	struct device *dev_ret;

	/* create a bdbm structure */
	if ((_bdm = (bdbm_dm_t*)bdbm_malloc_atomic (sizeof (bdbm_dm_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return -EIO;
	}

	/* create a tasklet */
	if ((_bdm->tasklet = (struct tasklet_struct*)bdbm_malloc_atomic 
			(sizeof (struct tasklet_struct))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		bdbm_free_atomic (_bdm);
		_bdm = NULL;
		return -EIO;
	}
	tasklet_init (_bdm->tasklet, __tasklet_handler_done, (unsigned long)_bdm);
	bdbm_msg ("registered a tasklet");

	init_waitqueue_head (&_bdm->wait_queue);

	/* create a workqueue */
	if ((_bdm->workqueue = create_workqueue ("dm_queue")) == NULL) {
		bdbm_error ("create_workqueue failed: %d\n", _bdm->devnum);
		bdbm_free_atomic (_bdm);
		_bdm = NULL;
		return ret;
	}

	/* register a character device */
	if ((ret = alloc_chrdev_region 
			(&dev, FIRST_MINOR, MINOR_CNT, BDBM_DM_IOCTL_NAME)) != 0) {
		bdbm_error ("DM IOCTL registration failed: %d\n", _bdm->devnum);
		bdbm_free_atomic (_bdm);
		_bdm = NULL;
		return ret;
	}
	cdev_init (&c_dev, &fops);

	if ((ret = cdev_add (&c_dev, dev, MINOR_CNT)) < 0) {
		bdbm_error ("DM IOCTL registration failed: %d\n", _bdm->devnum);
		bdbm_free_atomic (_bdm);
		_bdm = NULL;
		return ret;
	}

	if (IS_ERR (cl = class_create (THIS_MODULE, "char"))) {
		bdbm_error ("DM IOCTL registration failed: %d\n", _bdm->devnum);
		cdev_del (&c_dev);
		unregister_chrdev_region (dev, MINOR_CNT);
		bdbm_free_atomic (_bdm);
		_bdm = NULL;
		return PTR_ERR (cl);
	}

	if (IS_ERR (dev_ret = device_create (cl, NULL, dev, NULL, BDBM_DM_IOCTL_NAME))) {
		bdbm_error ("DM IOCTL registration failed: %d\n", _bdm->devnum);
		class_destroy (cl);
		cdev_del (&c_dev);
		unregister_chrdev_region (dev, MINOR_CNT);
		bdbm_free_atomic (_bdm);
		_bdm = NULL;
		return PTR_ERR (dev_ret);
	}
	bdbm_msg ("character device registration is done (%dn", _bdm->devnum);

	return 0;

#endif


#if defined (CONFIG_DEVICE_TYPE_RAMDRIVE)
	_param_device_type = DEVICE_TYPE_RAMDRIVE;
	bdbm_msg ("RAMDRIVE is detected");
#elif defined (CONFIG_DEVICE_TYPE_RAMDRIVE_INTR)
	_param_device_type = DEVICE_TYPE_RAMDRIVE_INTR;
	bdbm_msg ("RAMDRIVE with Interrupt is detected");
#elif defined (CONFIG_DEVICE_TYPE_RAMDRIVE_TIMING)
	_param_device_type = DEVICE_TYPE_RAMDRIVE_TIMING;
	bdbm_msg ("RAMDRIVE with Timining Emulation is detected");
#elif defined (CONFIG_DEVICE_TYPE_BLUEDBM)
	_param_device_type = DEVICE_TYPE_BLUEDBM;
	bdbm_msg ("BlueDBM is detected");
#elif defined (CONFIG_DEVICE_TYPE_USER_DUMMY)
	_param_device_type = DEVICE_TYPE_USER_DUMMY;
	bdbm_msg ("User-level Dummy Drive is detected");
#elif defined (CONFIG_DEVICE_TYPE_USER_RAMDRIVE)
	_param_device_type = DEVICE_TYPE_USER_RAMDRIVE;
	bdbm_msg ("User-level RAMDRIVE is detected");
#else
	#error Invalid HW is set
	_param_device_type = DEVICE_TYPE_NOTSET;
#endif

	bdbm_msg ("risa_dev_warpper is initialized");
	return 0;
}

#if defined (KERNEL_MODE)
static void __exit risa_dev_exit (void)
#elif defined (USER_MODE)
void risa_dev_exit (void)
#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif
{
	bdbm_msg ("risa_dev_warpper is destroyed");

#if defined (KERNEL_MODE)

#if 0
	unregister_chrdev (_bdm->devnum, BDBM_DM_IOCTL_NAME);
	init_waitqueue_head (&_bdm->wait_queue);
	tasklet_kill (_bdm->tasklet);
	bdbm_free_atomic (_bdm);
	_bdm = NULL;
#endif

	flush_workqueue (_bdm->workqueue);
	destroy_workqueue (_bdm->workqueue);

	device_destroy (cl, dev);
    class_destroy (cl);
    cdev_del (&c_dev);
    unregister_chrdev_region (dev, MINOR_CNT);
#endif
}

struct bdbm_dm_inf_t* setup_risa_device (struct bdbm_drv_info* bdi)
{
	if (bdi == NULL) {
		bdbm_warning ("bid is NULL");
		return NULL;
	}

#if !defined (KERNEL_MODE)
	risa_dev_init ();
#endif

	bdbm_msg ("A risa device is attached completely");

	/* setup the _bdi structure */
	_bdi_dm = bdi;

	/* return bdbm_dm_inf_t */
	return &_bdbm_dm_inf;
}

#if defined (KERNEL_MODE)
EXPORT_SYMBOL (setup_risa_device);

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("RISA Device Wrapper");
MODULE_LICENSE ("GPL");

module_init (risa_dev_init);
module_exit (risa_dev_exit);
#endif
