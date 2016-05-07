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
#include "dev_stub.h"

#elif defined (USER_MODE)
#include <stdio.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "bdbm_drv.h"
#include "debug.h"
#include "algo/abm.h"
#include "umemory.h"

extern bdbm_dm_inf_t _bdbm_dm_inf; /* exported by the device implementation module */
bdbm_drv_info_t* _bdi_dm = NULL; /* for Connectal & RAMSSD */


#if defined (KERNEL_MODE)
static int __init risa_dev_init (void)
{
	/* initialize dm_stub_proxy for user-level apps */
	bdbm_dm_stub_init ();
	return 0;
}

static void __exit risa_dev_exit (void)
{
	/* initialize dm_stub_proxy for user-level apps */
	bdbm_dm_stub_exit ();
}
#endif

int bdbm_dm_init (bdbm_drv_info_t* bdi)
{
	/* see if bdi is valid or not */
	if (bdi == NULL) {
		bdbm_warning ("bid is NULL");
		return 1;
	}

	if (_bdi_dm != NULL) {
		bdbm_warning ("dm_stub is already used by other clients");
		return 1;
	}

	/* initialize global variables */
	_bdi_dm = bdi;

	return 0;
}

void bdbm_dm_exit (bdbm_drv_info_t* bdi)
{
	_bdi_dm = NULL;
}

/* NOTE: Export dm_inf to kernel or user applications.
 * This is only supported when both the FTL and the device manager (dm) are compiled 
 * in the same mode (i.e., both KERNEL_MODE or USER_MODE) */
bdbm_dm_inf_t* bdbm_dm_get_inf (bdbm_drv_info_t* bdi)
{
	if (_bdi_dm == NULL) {
		bdbm_warning ("_bdi_dm is not initialized yet");
		return NULL;
	}

	return &_bdbm_dm_inf;
}

int bdbm_aggr_init (bdbm_drv_info_t* bdi)
{
	// TODO: make a variable to keep track of nr_blocks_in_use at the device level
	return bdbm_dm_init(bdi);
}

void bdbm_aggr_exit (bdbm_drv_info_t* bdi)
{
	bdbm_dm_exit(bdi);
}

bdbm_dm_inf_t* bdbm_aggr_get_inf (bdbm_drv_info_t* bdi)
{
	return bdbm_dm_get_inf(bdi);
}

void bdbm_inc_nr_blocks(bdbm_abm_info_t* bai, bdbm_abm_block_t** ac_bab)
{
	int loop, block_unit, offset, ac_idx;
	int *old_ac_blks = NULL;
	int channel_no, chip_no;
	bdbm_device_params_t *np = bai->np;
	bdbm_abm_block_t *bab;

	/* TODO: check if low level device have blocks to give */
	 

	block_unit = np->nr_channels * np->nr_chips_per_channel;

	if ((old_ac_blks = (int*)bdbm_zmalloc(sizeof(int) * block_unit)) == NULL) {
		bdbm_error("bdbm_malloc failed");
	}

	// store old location of active blocks
	for (loop = 0; loop < block_unit; loop++){
		bab = ac_bab[loop];
		channel_no = bab->channel_no;
		chip_no = bab->chip_no;
		old_ac_blks[loop] = channel_no * np->nr_chips_per_channel + chip_no;
	}
	ac_idx = block_unit-1;

	// re-ordering for new layout 
	for (loop = np->nr_blocks_per_ssd-1; loop > 0; loop--) {
		offset = loop / block_unit;
		memcpy(&(bai->blocks[loop+offset]), &(bai->blocks[loop]), sizeof(bdbm_abm_block_t));

		// TODO: need to check if moving the location of a block affects other parts, e.g. pointer to active block
		if(old_ac_blks[ac_idx] == loop) {
			if(ac_idx < 0) { bdbm_error("ac_idx at old_ac_blks is lower than 0"); continue;}
			ac_bab[ac_idx] = &(bai->blocks[loop+offset]);
			ac_idx--;
		}
	}
	if(ac_idx != -1) bdbm_error("ac_idx at old_ac_blks is not -1 at the end");		
	
	np->nr_blocks_per_chip++;
	np->nr_blocks_per_ssd = np->nr_channels * np->nr_chips_per_channel * np->nr_blocks_per_chip;

	for (loop = block_unit; loop < np->nr_blocks_per_ssd; loop += np->nr_blocks_per_chip) {
		// initialization for new blocks
		bai->blocks[loop].status = BDBM_ABM_BLK_FREE;
		bai->blocks[loop].channel_no = __get_channel_ofs (np, loop);
		bai->blocks[loop].chip_no = __get_chip_ofs (np, loop);
		bai->blocks[loop].block_no = __get_block_ofs (np, loop);
		bai->blocks[loop].erase_count = 0;
		bai->blocks[loop].pst = NULL;
		bai->blocks[loop].nr_invalid_subpages = 0;

		// add the new blocks to free list
		list_add_tail (&(bai->blocks[loop].list), 
				&(bai->list_head_free[bai->blocks[loop].channel_no][bai->blocks[loop].chip_no]));
	}
	bai->nr_total_blks = np->nr_blocks_per_ssd;
	bai->nr_free_blks += block_unit;

	bdbm_free(old_ac_blks);
}

void bdbm_dec_nr_blocks(bdbm_abm_info_t* bai, bdbm_abm_block_t** ac_bab)
{
	bdbm_device_params_t *np = bai->np;

	/* TODO: check if high level view have blocks to return */ 

	np->nr_blocks_per_chip--;
	np->nr_blocks_per_ssd = np->nr_channels * np->nr_chips_per_channel * np->nr_blocks_per_chip;
}


#if defined (KERNEL_MODE)
//EXPORT_SYMBOL (bdbm_dm_init);
//EXPORT_SYMBOL (bdbm_dm_exit);
//EXPORT_SYMBOL (bdbm_dm_get_inf);
EXPORT_SYMBOL (bdbm_aggr_init);
EXPORT_SYMBOL (bdbm_aggr_exit);
EXPORT_SYMBOL (bdbm_aggr_get_inf);
EXPORT_SYMBOL (bdbm_inc_nr_blocks);
EXPORT_SYMBOL (bdbm_dec_nr_blocks);

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("RISA Device Wrapper");
MODULE_LICENSE ("GPL");

module_init (risa_dev_init);
module_exit (risa_dev_exit);
#endif
