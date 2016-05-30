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
//#include "dm_ramdrive.h"
#include "debug.h"
#include "algo/abm.h"
#include "umemory.h"
#include "dev_params.h"

extern bdbm_dm_inf_t _bdbm_dm_inf; /* exported by the device implementation module */
bdbm_drv_info_t* _bdi_dm[NR_VOLUMES]; /* for Connectal & RAMSSD */

uint32_t dm_aggr_probe (bdbm_drv_info_t* bdi, bdbm_device_params_t* params);
uint32_t dm_aggr_open (bdbm_drv_info_t* bdi);
void dm_aggr_close (bdbm_drv_info_t* bdi);
uint32_t dm_aggr_make_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req);
uint32_t dm_aggr_make_reqs (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr);
void dm_aggr_end_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req);
uint32_t dm_aggr_load (bdbm_drv_info_t* bdi, const char* fn);
uint32_t dm_aggr_store (bdbm_drv_info_t* bdi, const char* fn);

bdbm_dm_inf_t _bdbm_aggr_inf = {
	.ptr_private = NULL,
	.probe = dm_aggr_probe,
	.open = dm_aggr_open,
	.close = dm_aggr_close,
	.make_req = dm_aggr_make_req,
	.make_reqs = dm_aggr_make_reqs,
	.end_req = dm_aggr_end_req,
	.load = dm_aggr_load,
	.store = dm_aggr_store,
};
#define FALSE 0
#define TRUE 1
uint8_t run_flag = FALSE;
uint8_t destroy_flag = FALSE;

uint64_t *bdbm_aggr_mapping = NULL;
uint64_t cur_sblock = 0;
uint8_t *bdbm_aggr_pblock_status = NULL;
enum BDBM_AGGR_PBLOCK_STATUS {
	AGGR_PBLOCK_FREE = 0,
	AGGR_PBLOCK_ALLOCATED,
};

bdbm_sema_t aggr_lock;

uint64_t __get_aggr_idx (bdbm_device_params_t* np, uint32_t vol, uint64_t blk_no) {
	bdbm_bug_on (blk_no >= np->nr_blocks_per_chip);
	bdbm_bug_on (vol >= np->nr_volumes);
	return np->nr_blocks_per_chip * vol + blk_no;
}


#if defined (KERNEL_MODE)
static int __init risa_dev_init (void)
{
	/* initialize dm_stub_proxy for user-level apps */
	//bdbm_dm_stub_init ();
	return 0;
}

static void __exit risa_dev_exit (void)
{
	/* initialize dm_stub_proxy for user-level apps */
	//bdbm_dm_stub_exit ();
}
#endif

#if 0
int bdbm_dm_init (bdbm_drv_info_t* bdi)
{
	/* see if bdi is valid or not */
	if (bdi == NULL) {
		bdbm_warning ("bid is NULL");
		return 1;
	}

	if (_bdi_dm != NULL) {
		// need to check? what if it is already allocated.
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
#endif

int bdbm_aggr_init (bdbm_drv_info_t* bdi, uint8_t volume)
{
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	uint64_t nr_virt_blocks = np->nr_blocks_per_chip * np->nr_volumes;

	if(bdbm_aggr_mapping == NULL) {
		// TODO: mapping table for blocks between volumes and physical device
		if ((bdbm_aggr_mapping = (uint64_t*)bdbm_zmalloc(sizeof(uint64_t) * nr_virt_blocks)) == NULL) {
			bdbm_error ("bdbm_zmalloc failed");
			goto fail;
		}
		memset(bdbm_aggr_mapping, 0x00, sizeof(uint64_t) * nr_virt_blocks);
	}

	if(bdbm_aggr_pblock_status == NULL){
		// TODO: flag for physical block to check if the block is allocated to a volume
		if ((bdbm_aggr_pblock_status = (uint8_t*)bdbm_zmalloc(sizeof(uint8_t) * np->nr_blocks_per_ssd)) == NULL) {
			bdbm_error ("bdbm_zmalloc failed");
			goto fail;
		}
	}

	/* see if bdi is valid or not */
	if (bdi == NULL) {
		bdbm_warning ("bid is NULL");
		return 1;
	}

	// attach device related interface, such as dm_probe, dm_open, to bdi
	bdi->ptr_dm_inf = &_bdbm_aggr_inf;

	if (run_flag == FALSE) {
		/* initialize global variables */
		run_flag = TRUE;

		/* run device setup functions once */
		if (bdi->ptr_dm_inf) {
			uint32_t load;
			bdbm_dm_inf_t* dm = bdi->ptr_dm_inf;

			/* get the device information */
			if (dm->probe == NULL || dm->probe (bdi, &bdi->parm_dev) != 0) {
				bdbm_error ("[bdbm_drv_main] failed to probe a flash device");
				goto fail;
			}
			/* open a flash device */
			if (dm->open == NULL || dm->open (bdi) != 0) {
				bdbm_error ("[bdbm_drv_main] failed to open a flash device");
				goto fail;
			}
			/* do we need to read a snapshot? */
			if (bdi->parm_ftl.snapshot == SNAPSHOT_ENABLE &&
					dm->load != NULL) {
				if (dm->load (bdi, "/usr/share/bdbm_drv/dm.dat") != 0) {
					bdbm_msg ("[bdbm_drv_main] loading 'dm.dat' failed");
					load = 0;
				} else 
					load = 1;
			}
		}

		bdbm_sema_init(&aggr_lock);
		bdbm_memset(_bdi_dm, 0x00, sizeof(bdbm_drv_info_t*) * NR_VOLUMES);
	}
	bdbm_bug_on(volume >= NR_VOLUMES);
	_bdi_dm[volume] = bdi;

	return 0;
	//return bdbm_dm_init(bdi);

fail: 
	if(bdbm_aggr_mapping)
		bdbm_free(bdbm_aggr_mapping);
	if(bdbm_aggr_pblock_status)
		bdbm_free(bdbm_aggr_pblock_status);
	return 1;
}

void bdbm_aggr_exit (bdbm_drv_info_t* bdi)
{
	if(destroy_flag == FALSE) {
		destroy_flag = TRUE;

		if(bdbm_aggr_mapping != NULL) 
			bdbm_free(bdbm_aggr_mapping);

		if(bdbm_aggr_pblock_status != NULL)
			bdbm_free(bdbm_aggr_pblock_status);

		bdbm_sema_free(&aggr_lock);
	}
}

bdbm_dm_inf_t* bdbm_aggr_get_inf (bdbm_drv_info_t* bdi)
{
	return &_bdbm_aggr_inf;
}

uint32_t bdbm_aggr_allocate_blocks(bdbm_device_params_t *np, uint64_t block_no, uint32_t volume)
{
	uint32_t loop = 0;
	uint64_t aggr_idx;
	//uint32_t nr_punit;
	
	if(bdbm_aggr_mapping == NULL) {
		bdbm_error ("bdbm_aggr_mapping is NULL");
		return 1;
	}

	if(bdbm_aggr_pblock_status == NULL) {
		bdbm_error ("bdbm_aggr_pblock_status is NULL");
		return 1;
	}

	if(block_no >= np->nr_blocks_per_chip) {
		bdbm_error ("block_no (%llu) is larger than # of blocks per chip", block_no);
		return 1;
	}

	if(volume >= np->nr_volumes) {
		bdbm_error ("volume (%d) is larger than # of volumes", volume);
		return 1;
	}

	while (bdbm_aggr_pblock_status[cur_sblock] != AGGR_PBLOCK_FREE) {
		loop++;
		cur_sblock++;
		if(cur_sblock == np->nr_blocks_per_chip) 
			cur_sblock = 0;
		// check for infinite loop
		if(loop >= np->nr_blocks_per_chip) {
			bdbm_error ("There is no free super block to allocate for volume %d", volume);
			return 1;
		}
	}

	aggr_idx = __get_aggr_idx(np, volume, block_no);
	bdbm_bug_on(aggr_idx >= (np->nr_blocks_per_chip * np->nr_volumes));
	bdbm_bug_on(bdbm_aggr_mapping[aggr_idx] != 0);
	bdbm_aggr_mapping[aggr_idx] = cur_sblock;
	bdbm_aggr_pblock_status[cur_sblock] = AGGR_PBLOCK_ALLOCATED;
	
	//bdbm_msg("aggr allocation volume: %d, pblock: %llu", volume, cur_sblock);
	return 0;
}

uint32_t bdbm_aggr_return_blocks(bdbm_device_params_t *np, uint64_t block_no, uint32_t volume) {
	uint64_t aggr_idx, tblk_num;

	if(bdbm_aggr_mapping == NULL) {
		bdbm_error ("bdbm_aggr_mapping is NULL");
		return 1;
	}

	if(bdbm_aggr_pblock_status == NULL) {
		bdbm_error ("bdbm_aggr_pblock_status is NULL");
		return 1;
	}

	if(block_no >= np->nr_blocks_per_chip) {
		bdbm_error ("block_no (%llu) is larger than # of blocks per chip", block_no);
		return 1;
	}

	if(volume >= np->nr_volumes) {
		bdbm_error ("volume (%d) is larger than # of volumes", volume);
		return 1;
	}

	aggr_idx = __get_aggr_idx(np, volume, block_no);
	bdbm_bug_on(aggr_idx >= (np->nr_blocks_per_chip * np->nr_volumes));
	tblk_num = bdbm_aggr_mapping[aggr_idx];
	bdbm_aggr_mapping[aggr_idx] = 0;
	bdbm_bug_on(tblk_num >= np->nr_blocks_per_chip);
	bdbm_bug_on(bdbm_aggr_pblock_status[tblk_num] != AGGR_PBLOCK_ALLOCATED);
	bdbm_aggr_pblock_status[tblk_num] = AGGR_PBLOCK_FREE;

	return 0;
}

#if 0
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
#endif


uint32_t dm_aggr_probe (bdbm_drv_info_t* bdi, bdbm_device_params_t* params) {
	return _bdbm_dm_inf.probe(bdi, params);
}

uint32_t dm_aggr_open (bdbm_drv_info_t* bdi) {
	return _bdbm_dm_inf.open(bdi);
}

void dm_aggr_close (bdbm_drv_info_t* bdi) {
	return _bdbm_dm_inf.close(bdi);
}

uint32_t dm_aggr_make_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req){
	uint32_t volume = ptr_llm_req->volume;
	uint64_t org_block_no = ptr_llm_req->phyaddr.block_no;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	uint64_t aggr_idx = __get_aggr_idx(np, volume, org_block_no);

	// translate block number
	bdbm_bug_on (bdbm_aggr_pblock_status[bdbm_aggr_mapping[aggr_idx]] == AGGR_PBLOCK_FREE);
	ptr_llm_req->phyaddr.block_no = bdbm_aggr_mapping[aggr_idx];
/*
	bdbm_msg("aggr make_req, volume: %d, vblock: %llu, pblock: %llu", 
			volume, org_block_no, bdbm_aggr_mapping[aggr_idx]);
*/
	return _bdbm_dm_inf.make_req(bdi, ptr_llm_req);
}

uint32_t dm_aggr_make_reqs (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr){
	uint32_t volume = hr->volume;
	uint32_t i;
	bdbm_llm_req_t* lr = NULL;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
#ifdef TIMELINE_DEBUG_TJKIM
	bdbm_stopwatch_t aggr_sw;

	bdbm_stopwatch_start(&aggr_sw);
#endif

	// translate block number
	bdbm_hlm_for_each_llm_req (lr, hr, i) {
		uint64_t org_block_no = lr->phyaddr.block_no;
		uint64_t aggr_idx = __get_aggr_idx(np, volume, org_block_no);
		bdbm_bug_on (bdbm_aggr_pblock_status[bdbm_aggr_mapping[aggr_idx]] == AGGR_PBLOCK_FREE);
		lr->phyaddr.block_no = bdbm_aggr_mapping[aggr_idx];
		/*
		if(i == 0) {
			bdbm_msg("aggr make_reqs, volume: %d, vblock: %llu, pblock: %llu", 
					volume, org_block_no, bdbm_aggr_mapping[aggr_idx]);
		}
		*/
	}

#ifdef TIMELINE_DEBUG_TJKIM
	bdbm_msg("volume: %d, aggr elapsed time: %llu", volume, bdbm_stopwatch_get_elapsed_time_us(&aggr_sw));
#endif
	return _bdbm_dm_inf.make_reqs(bdi, hr);
}
void dm_aggr_end_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req) {
	bdi->ptr_llm_inf->end_req(bdi, ptr_llm_req);
	//return _bdbm_dm_inf.end_req(bdi, ptr_llm_req);
}

uint32_t dm_aggr_load (bdbm_drv_info_t* bdi, const char* fn) {
	return _bdbm_dm_inf.load(bdi, fn);
}

uint32_t dm_aggr_store (bdbm_drv_info_t* bdi, const char* fn) {
	return _bdbm_dm_inf.store(bdi, fn);
}


void bdbm_aggr_lock(void) {
	bdbm_sema_lock(&aggr_lock);
}

void bdbm_aggr_unlock(void) {
	bdbm_sema_unlock(&aggr_lock);
}

#if defined (KERNEL_MODE)
//EXPORT_SYMBOL (bdbm_dm_init);
//EXPORT_SYMBOL (bdbm_dm_exit);
//EXPORT_SYMBOL (bdbm_dm_get_inf);
EXPORT_SYMBOL (bdbm_aggr_init);
EXPORT_SYMBOL (bdbm_aggr_exit);
EXPORT_SYMBOL (bdbm_aggr_get_inf);
EXPORT_SYMBOL (bdbm_aggr_allocate_blocks);
EXPORT_SYMBOL (bdbm_aggr_return_blocks);
EXPORT_SYMBOL (bdbm_aggr_lock);
EXPORT_SYMBOL (bdbm_aggr_unlock);

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("RISA Device Wrapper");
MODULE_LICENSE ("GPL");

module_init (risa_dev_init);
module_exit (risa_dev_exit);
#endif
