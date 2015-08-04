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
#include <linux/module.h>

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

/* It must be exported by the device implementation module */
extern struct bdbm_dm_inf_t _bdbm_dm_inf; 

/* It is used by the device implementation module */
struct bdbm_drv_info* _bdi_dm = NULL;

#if defined (KERNEL_MODE)
static int __init risa_dev_init (void)
#elif defined (USER_MODE)
int risa_dev_init (void)
#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif
{
	bdbm_msg ("risa_dev_warpper is initialized");

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
}

/*extern struct bdbm_drv_info* _bdi_dm;*/

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
