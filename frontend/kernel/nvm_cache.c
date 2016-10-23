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

#include <linux/module.h> /* uint64_t */
//#include <linux/blkdev.h> /* bio */
//#include <linux/hdreg.h>
//#include <linux/kthread.h>
//#include <linux/delay.h> /* mdelay */

#include "bdbm_drv.h"
#include "debug.h"
//#include "blkdev.h"
//#include "blkdev_ioctl.h"
//#include "umemory.h"

#ifdef NVM_CACHE

#include "nvm_cache.h"

/* interface for nvm_dev */
bdbm_nvm_inf_t _nvm_dev = {
	.ptr_private = NULL,
	.create = bdbm_nvm_create,
//	.destroy = nvm_destroy,
//	.make_req = nvm_make_req,
//	.end_req = nvm_end_req,
};

uint32_t bdbm_nvm_create (bdbm_drv_info_t* bdi){
//	if (bdi->ptr_nvm_inf)
	bdbm_msg("create succeeds");
	return 0;
}

#endif
