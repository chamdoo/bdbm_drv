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

#include <linux/module.h>

#include "debug.h"
#include "dm_ramdrive.h"
#include "dm_params.h"
#include "dev_ramssd.h"

#include "utils/utime.h"


/* interface for dm */
struct bdbm_dm_inf_t _bdbm_dm_inf = {
	.ptr_private = NULL,
	.probe = dm_ramdrive_probe,
	.open = dm_ramdrive_open,
	.close = dm_ramdrive_close,
	.make_req = dm_ramdrive_make_req,
	.end_req = dm_ramdrive_end_req,
	.load = dm_ramdrive_load,
	.store = dm_ramdrive_store,
};

/* private data structure for dm */
struct dm_ramssd_private {
	struct dev_ramssd_info *ramssd;
};

/* global data structure */
extern struct bdbm_drv_info* _bdi;

/* interrupt handler */
static void __dm_ramdrive_ih (void* arg)
{
	struct bdbm_llm_req_t* ptr_llm_req = (struct bdbm_llm_req_t*)arg;
	struct bdbm_drv_info* bdi = _bdi;

	bdi->ptr_dm_inf->end_req (bdi, ptr_llm_req);
}

static void __dm_setup_device_params (struct nand_params* params)
{
	/* user-specified parameters */
	params->nr_channels = _param_nr_channels;
	params->nr_chips_per_channel = _param_nr_chips_per_channel;
	params->nr_blocks_per_chip = _param_nr_blocks_per_chip;
	params->nr_pages_per_block = _param_nr_pages_per_block;
	params->page_main_size = _param_page_main_size;
	params->page_oob_size = _param_page_oob_size;
	params->device_type = _param_device_type;
	params->page_prog_time_us = _param_page_prog_time_us;
	params->page_read_time_us = _param_page_read_time_us;
	params->block_erase_time_us = _param_block_erase_time_us;
	/*params->timing_mode = _param_ramdrv_timing_mode;*/

	/* other parameters derived from user parameters */
	params->nr_blocks_per_channel = 
		params->nr_chips_per_channel * 
		params->nr_blocks_per_chip;

	params->nr_blocks_per_ssd = 
		params->nr_channels * 
		params->nr_chips_per_channel * 
		params->nr_blocks_per_chip;

	params->nr_chips_per_ssd =
		params->nr_channels * 
		params->nr_chips_per_channel;

	params->nr_pages_per_ssd =
		params->nr_pages_per_block * 
		params->nr_blocks_per_ssd;

	params->device_capacity_in_byte = 0;
	params->device_capacity_in_byte += params->nr_channels;
	params->device_capacity_in_byte *= params->nr_chips_per_channel;
	params->device_capacity_in_byte *= params->nr_blocks_per_chip;
	params->device_capacity_in_byte *= params->nr_pages_per_block;
	params->device_capacity_in_byte *= params->page_main_size;
}

uint32_t dm_ramdrive_probe (struct bdbm_drv_info* bdi, struct nand_params* params)
{
	struct dm_ramssd_private* p = NULL;

	/* setup NAND parameters according to users' inputs */
	__dm_setup_device_params (params);

	/* create a private structure for ramdrive */
	if ((p = (struct dm_ramssd_private*)bdbm_malloc_atomic
			(sizeof (struct dm_ramssd_private))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail;
	}

	/* create RAMSSD based on user-specified NAND parameters */
	if ((p ->ramssd = dev_ramssd_create (
			/*&bdi->ptr_bdbm_params->nand, */
			/*bdi->ptr_bdbm_params->nand.timing_mode, */
			params,	__dm_ramdrive_ih)) == NULL) {
		bdbm_error ("dev_ramssd_create failed");
		bdbm_free_atomic (p);
		goto fail;
	} 
	bdbm_msg ("ramssd is detected!");

	/* display RAMSSD */
	/*dev_ramssd_summary (p->ramssd);*/

	/* OK! keep private info */
	bdi->ptr_dm_inf->ptr_private = (void*)p;

	return 0;

fail:
	return -1;
}

uint32_t dm_ramdrive_open (struct bdbm_drv_info* bdi)
{
	struct dm_ramssd_private * p;

	p = (struct dm_ramssd_private*)bdi->ptr_dm_inf->ptr_private;

	bdbm_msg ("dm_ramdrive_open is initialized");

	return dev_ramssd_is_init (p->ramssd);
}

void dm_ramdrive_close (struct bdbm_drv_info* bdi)
{
	struct dm_ramssd_private* p; 

	p = (struct dm_ramssd_private*)bdi->ptr_dm_inf->ptr_private;

	bdbm_msg ("dm_ramdrive_close is destroyed");

	dev_ramssd_destroy (p->ramssd);

	bdbm_free_atomic (p);
}

uint32_t dm_ramdrive_make_req (struct bdbm_drv_info* bdi, struct bdbm_llm_req_t* ptr_llm_req)
{
	uint32_t ret;
	struct dm_ramssd_private* p; 

	p = (struct dm_ramssd_private*)bdi->ptr_dm_inf->ptr_private;

	if ((ret = dev_ramssd_send_cmd (p->ramssd, ptr_llm_req)) != 0) {
		bdbm_error ("dev_ramssd_send_cmd failed");
		/* there is nothing to do */
	}

	return ret;
}

void dm_ramdrive_end_req (struct bdbm_drv_info* bdi, struct bdbm_llm_req_t* ptr_llm_req)
{
	bdbm_bug_on (ptr_llm_req == NULL);

	bdi->ptr_llm_inf->end_req (bdi, ptr_llm_req);
}

/* for snapshot */
uint32_t dm_ramdrive_load (struct bdbm_drv_info* bdi, const char* fn)
{	
	struct dm_ramssd_private * p = 
		(struct dm_ramssd_private*)bdi->ptr_dm_inf->ptr_private;
	bdbm_msg ("loading a DRAM snapshot...");
	return dev_ramssd_load (p->ramssd, fn);
}

uint32_t dm_ramdrive_store (struct bdbm_drv_info* bdi, const char* fn)
{
	struct dm_ramssd_private * p = 
		(struct dm_ramssd_private*)bdi->ptr_dm_inf->ptr_private;
	bdbm_msg ("storing a DRAM snapshot...");
	return dev_ramssd_store (p->ramssd, fn);
}

