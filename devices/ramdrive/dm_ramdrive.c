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

#include "debug.h"
#include "dm_ramdrive.h"
#include "dev_params.h"
#include "dev_ramssd.h"

#include "utime.h"
#include "umemory.h"


/* interface for dm */
bdbm_dm_inf_t _bdbm_dm_inf = {
	.ptr_private = NULL,
	.probe = dm_ramdrive_probe,
	.open = dm_ramdrive_open,
	.close = dm_ramdrive_close,
	.make_req = dm_ramdrive_make_req,
	.make_reqs = dm_ramdrive_make_reqs,
	.end_req = dm_ramdrive_end_req,
	.load = dm_ramdrive_load,
	.store = dm_ramdrive_store,
};

/* private data structure for dm */
typedef struct {
	dev_ramssd_info_t *ramssd;
} dm_ramssd_private_t;

/* global data structure */
extern bdbm_drv_info_t* _bdi_dm[NR_VOLUMES];

/* interrupt handler */
static void __dm_ramdrive_ih (void* arg)
{
	bdbm_llm_req_t* ptr_llm_req = (bdbm_llm_req_t*)arg;
	uint8_t volume = ptr_llm_req->volume;
	bdbm_drv_info_t* bdi = _bdi_dm[volume];

	bdbm_bug_on(bdi == NULL);
	bdbm_bug_on(volume >= NR_VOLUMES);

#ifdef TIMELINE_DEBUG_TJKIM
	bdbm_msg("__dm_ramdrive_ih is called");
#endif
	if(bdi->ptr_dm_inf->end_req) {
		bdi->ptr_dm_inf->end_req (bdi, ptr_llm_req);
	}
	else bdbm_error("end_req is not allocated");
}

static void __dm_setup_device_params (bdbm_device_params_t* params)
{
	*params = get_default_device_params ();
}

uint32_t dm_ramdrive_probe (bdbm_drv_info_t* bdi, bdbm_device_params_t* params)
{
	dm_ramssd_private_t* p = NULL;

	/* setup NAND parameters according to users' inputs */
	__dm_setup_device_params (params);

	display_device_params (params);

	/* create a private structure for ramdrive */
	if ((p = (dm_ramssd_private_t*)bdbm_malloc_atomic (sizeof (dm_ramssd_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail;
	}

	/* create RAMSSD based on user-specified NAND parameters */
	if ((p->ramssd = dev_ramssd_create (
			params,	__dm_ramdrive_ih)) == NULL) {
		bdbm_error ("dev_ramssd_create failed");
		bdbm_free_atomic (p);
		goto fail;
	} 

	/* OK! keep private info */
	bdi->ptr_dm_inf->ptr_private = (void*)p;

	bdbm_msg ("[dm_ramdrive_probe] probe done!");

	return 0;

fail:
	return -1;
}

uint32_t dm_ramdrive_open (bdbm_drv_info_t* bdi)
{
	dm_ramssd_private_t * p = BDBM_DM_PRIV (bdi);

	/*p = (dm_ramssd_private_t*)bdi->ptr_dm_inf->ptr_private;*/

	bdbm_msg ("[dm_ramdrive_open] open done!");

	return dev_ramssd_is_init (p->ramssd);
}

void dm_ramdrive_close (bdbm_drv_info_t* bdi)
{
	dm_ramssd_private_t* p = BDBM_DM_PRIV (bdi);


	if(p != NULL) {
		if(p->ramssd != NULL) {
			dev_ramssd_destroy (p->ramssd);
		}

		bdbm_free_atomic (p);
		bdbm_msg ("[dm_ramdrive_close] closed!");
	}
}

uint32_t dm_ramdrive_make_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req)
{
	uint32_t ret;
	dm_ramssd_private_t* p = BDBM_DM_PRIV (bdi);

	if ((ret = dev_ramssd_send_cmd (p->ramssd, ptr_llm_req)) != 0) {
		bdbm_error ("dev_ramssd_send_cmd failed");
		/* there is nothing to do */
	}

	return ret;
}

uint32_t dm_ramdrive_make_reqs (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr)
{
#include "../../ftl/hlm_reqs_pool.h"

	uint32_t i, ret = 1;
	bdbm_llm_req_t* lr = NULL;
	dm_ramssd_private_t* p = BDBM_DM_PRIV (bdi);
#ifdef TIMELINE_DEBUG_TJKIM
	bdbm_stopwatch_t dm_sw;

	bdbm_stopwatch_start(&dm_sw);
#endif
	bdbm_hlm_for_each_llm_req (lr, hr, i) {
		if ((ret = dev_ramssd_send_cmd (p->ramssd, lr)) != 0) {
			bdbm_error ("dev_ramssd_send_cmd failed");
			break;
		}
	}

#ifdef TIMELINE_DEBUG_TJKIM
	bdbm_msg("dm elapsed time: %llu", bdbm_stopwatch_get_elapsed_time_us(&dm_sw));
#endif
	return ret;
}

void dm_ramdrive_end_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req)
{
	bdbm_bug_on (ptr_llm_req == NULL);
	bdbm_msg("oops! tjkim, dm_ramdrive_end_req is called");
	bdi->ptr_llm_inf->end_req (bdi, ptr_llm_req);
}

/* for snapshot */
uint32_t dm_ramdrive_load (bdbm_drv_info_t* bdi, const char* fn)
{	
	dm_ramssd_private_t* p = BDBM_DM_PRIV (bdi);
	bdbm_msg ("loading a DRAM snapshot...");
	return dev_ramssd_load (p->ramssd, fn);
}

uint32_t dm_ramdrive_store (bdbm_drv_info_t* bdi, const char* fn)
{
	dm_ramssd_private_t* p = BDBM_DM_PRIV (bdi);
	bdbm_msg ("storing a DRAM snapshot...");
	return dev_ramssd_store (p->ramssd, fn);
}

