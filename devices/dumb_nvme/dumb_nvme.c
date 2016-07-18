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
#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "debug.h"
#include "dumb_nvme.h"
#include "dev_params.h"
#include "utime.h"
#include "umemory.h"


/* interface for dm */
bdbm_dm_inf_t _bdbm_dm_inf = {
	.ptr_private = NULL,
	.probe = dm_nvme_probe,
	.open = dm_nvme_open,
	.close = dm_nvme_close,
	.make_req = dm_nvme_make_req,
	.make_reqs = dm_nvme_make_reqs,
	.end_req = dm_nvme_end_req,
};

/* private data structure for dm */
typedef struct {

} dm_nvme_private_t;

/* global data structure */
extern bdbm_drv_info_t* _bdi_dm;


uint32_t dm_nvme_probe (
	bdbm_drv_info_t* bdi, 
	bdbm_device_params_t* params)
{
	dm_nvme_private_t* p = NULL;

	/* setup NAND parameters according to users' inputs */
	*params = get_default_device_params ();
	display_device_params (params);

	/* create a private structure for ramdrive */
	if ((p = (dm_nvme_private_t*)bdbm_malloc_atomic (
			sizeof (dm_nvme_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail;
	}

#if 0
	/* create RAMSSD based on user-specified NAND parameters */
	if ((p->ramssd = dev_ramssd_create (
			params,	__dm_ramdrive_ih)) == NULL) {
		bdbm_error ("dev_ramssd_create failed");
		bdbm_free_atomic (p);
		goto fail;
	} 
#endif

	/* OK! keep private info */
	bdi->ptr_dm_inf->ptr_private = (void*)p;

	bdbm_msg ("[dm_nvme_probe] probe done!");

	return 0;

fail:
	return -1;
}

uint32_t dm_nvme_open (
	bdbm_drv_info_t* bdi)
{
	dm_nvme_private_t * p = BDBM_DM_PRIV (bdi);
	bdbm_msg ("[dm_nvme_open] open done!");
	return 0;
}

void dm_nvme_close (
	bdbm_drv_info_t* bdi)
{
	dm_nvme_private_t* p = BDBM_DM_PRIV (bdi);
	bdbm_msg ("[dm_nvme_close] closed!");
	bdbm_free_atomic (p);
}

uint32_t dm_nvme_make_req (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r)
{
	bdbm_msg ("[dm_nvme_make_req]");
	return -1;
}

uint32_t dm_nvme_make_reqs (
	bdbm_drv_info_t* bdi, 
	bdbm_hlm_req_t* hr)
{
	bdbm_msg ("[dm_nvme_make_reqs]");
	return -1;
}

void dm_nvme_end_req (
	bdbm_drv_info_t* bdi,
	bdbm_llm_req_t* r)
{
	bdi->ptr_llm_inf->end_req (bdi, r);
}
