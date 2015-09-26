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

#include <linux/kernel.h>

#include "raw-flash.h"
#include "platform.h"
#include "debug.h"
#include "hw.h"


void __dm_intr_handler (bdbm_drv_info_t* bdi, bdbm_llm_req_t* r);

bdbm_llm_inf_t _bdbm_llm_inf = {
	.ptr_private = NULL,
	.create = NULL,
	.destroy = NULL,
	.make_req = NULL,
	.flush = NULL,
	.end_req = __dm_intr_handler, /* 'dm' automatically calls 'end_req' when it gets acks from devices */
};

void __dm_intr_handler (bdbm_drv_info_t* bdi, bdbm_llm_req_t* r)
{
}

bdbm_params_t* read_driver_params (void)
{
	bdbm_params_t* ptr_params = NULL;

	/* allocate the memory for parameters */
	if ((ptr_params = (bdbm_params_t*)bdbm_zmalloc (sizeof (bdbm_params_t))) == NULL) {
		bdbm_error ("failed to allocate the memory for ptr_params");
		return NULL;
	}

	/* setup driver parameters */
#if 0
	ptr_params->driver.mapping_policy = ;
	ptr_params->driver.gc_policy = ;
	ptr_params->driver.wl_policy = ;
	ptr_params->driver.kernel_sector_size = ;
	ptr_params->driver.trim = ;
	ptr_params->driver.host_type = ; 
	ptr_params->driver.llm_type = ;
	ptr_params->driver.hlm_type = ;
	ptr_params->driver.mapping_type = ;
#endif

	return ptr_params;
}


void __bdbm_raw_flash_destory (bdbm_raw_flash_t* rf)
{
	if (!rf)
		return;

	if (rf->bdi.ptr_bdbm_params)
		bdbm_free (rf->bdi.ptr_bdbm_params);
	bdbm_free (rf);
}

void __bdbm_raw_flash_show_nand_params (bdbm_params_t* p)
{
	bdbm_msg ("=====================================================================");
	bdbm_msg ("< NAND PARAMETERS >");
	bdbm_msg ("=====================================================================");
	bdbm_msg ("# of channels = %llu", p->nand.nr_channels);
	bdbm_msg ("# of chips per channel = %llu", p->nand.nr_chips_per_channel);
	bdbm_msg ("# of blocks per chip = %llu", p->nand.nr_blocks_per_chip);
	bdbm_msg ("# of pages per block = %llu", p->nand.nr_pages_per_block);
	bdbm_msg ("page main size  = %llu bytes", p->nand.page_main_size);
	bdbm_msg ("page oob size = %llu bytes", p->nand.page_oob_size);
	bdbm_msg ("SSD type = %u (0: ramdrive, 1: ramdrive with timing , 2: BlueDBM(emul), 3: BlueDBM)", p->nand.device_type);
	bdbm_msg ("");
}

bdbm_raw_flash_t* bdbm_raw_flash_init (void)
{
	bdbm_raw_flash_t* rf = NULL;
	bdbm_drv_info_t* bdi = NULL;
	bdbm_dm_inf_t* dm = NULL;

	/* create bdbm_raw_flash_t */
	if ((rf = bdbm_zmalloc (sizeof (bdbm_raw_flash_t))) == NULL) {
		bdbm_error ("bdbm_zmalloc () failed");
		goto fail;
	}
	bdi = &rf->bdi; /* for convenience */

	/* create params_t */
	if ((bdi->ptr_bdbm_params = (bdbm_params_t*)bdbm_malloc (sizeof (bdbm_params_t))) == NULL) {
		bdbm_error ("bdbm_malloc () failed");
		goto fail;
	}

	/* obtain dm_inf from the device */
	if (bdbm_dm_init (bdi) != 0)  {
		bdbm_error ("bdbm_dm_init () failed");
		goto fail;
	}
	if ((dm = bdbm_dm_get_inf (bdi)) == NULL) {
		bdbm_error ("bdbm_dm_get_inf () failed");
		goto fail;
	}
	bdi->ptr_dm_inf = dm;

	/* probe the device and get the device paramters */
	if (dm->probe (bdi, &bdi->ptr_bdbm_params->nand) != 0) {
		bdbm_error ("dm->probe () failed");
		goto fail;
	} else {
		__bdbm_raw_flash_show_nand_params (bdi->ptr_bdbm_params);
	}

	/* setup function points */
	bdi->ptr_llm_inf = &_bdbm_llm_inf;

	/* assign rf to bdi's private_data */
	bdi->private_data = (void*)rf;

	return rf;

fail:
	/* oops! it fails */
	__bdbm_raw_flash_destory (rf);
	return NULL;
}

int bdbm_raw_flash_open (bdbm_raw_flash_t* rf)
{
	bdbm_drv_info_t* bdi = &rf->bdi;
	bdbm_dm_inf_t* dm = bdi->ptr_dm_inf;

	/* open the device */
	if (dm->open (bdi) != 0) {
		bdbm_error ("dm->open () failed");
		return -1;
	}

	return 0;
}

void bdbm_raw_flash_exit (bdbm_raw_flash_t* rf)
{
	if (rf) {
		bdbm_drv_info_t* bdi = &rf->bdi;
		bdbm_dm_inf_t* dm = bdi->ptr_dm_inf;

		/* close the device interface */
		dm->close (bdi);

		/* close the device module */
		bdbm_dm_exit (bdi);

		/* destory the raw-flash module */
		__bdbm_raw_flash_destory (rf);
	}
}

