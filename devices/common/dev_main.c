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
#include "uthread.h"

#include "dev_params.h"
#include "queue/prior_queue.h"
#include "queue/queue.h"

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
	//.make_reqs = dm_aggr_make_reqs,
	.end_req = dm_aggr_end_req,
	.load = dm_aggr_load,
	.store = dm_aggr_store,
};
#define FALSE 0
#define TRUE 1
uint8_t run_flag = FALSE;
uint8_t destroy_flag = FALSE;

uint64_t *bdbm_aggr_mapping = NULL;
uint64_t *cur_pblock = NULL;
uint8_t *bdbm_aggr_pblock_status = NULL;
enum BDBM_AGGR_PBLOCK_STATUS {
	AGGR_PBLOCK_FREE = 0,
	AGGR_PBLOCK_ALLOCATED,
};

bdbm_sema_t aggr_lock;
bdbm_sema_t* bdbm_aggr_punit_locks = NULL;

bdbm_queue_t* aggr_rq = NULL;
//bdbm_prior_queue_t* aggr_rq = NULL;
bdbm_thread_t* aggr_thread = NULL;

//#define ENABLE_SEQ_DBG

#if defined(ENABLE_SEQ_DBG)
	bdbm_sema_t dbg_seq;
#endif	

#define PRIORITY_Q


uint64_t __convert_to_unique_lpa (uint64_t lpa, uint32_t vol) {
	uint64_t temp = (uint64_t)vol;
	uint64_t converted_lpa = lpa | (temp << 60);
	//bdbm_msg("lpa: %llx, v: %d, conv_lpa: %llx", lpa, vol, converted_lpa);
	return converted_lpa;
}

uint64_t __get_aggr_vblock_idx (bdbm_device_params_t* np, uint32_t vol, uint64_t channel_no, uint64_t chip_no, uint64_t blk_no) {
	bdbm_bug_on (blk_no >= np->nr_blocks_per_chip);
	bdbm_bug_on (vol >= np->nr_volumes);
	bdbm_bug_on (channel_no >= np->nr_channels);
	bdbm_bug_on (chip_no >= np->nr_chips_per_channel);
	return (np->nr_blocks_per_ssd * vol) + (np->nr_blocks_per_channel * channel_no) +
		(np->nr_blocks_per_chip * chip_no) + blk_no;
}

uint64_t __get_aggr_pblock_idx (bdbm_device_params_t* np, uint64_t channel_no, uint64_t chip_no, uint64_t blk_no) {
	bdbm_bug_on (blk_no >= np->nr_blocks_per_chip);
	bdbm_bug_on (channel_no >= np->nr_channels);
	bdbm_bug_on (chip_no >= np->nr_chips_per_channel);
	return (np->nr_blocks_per_channel * channel_no) + (np->nr_blocks_per_chip * chip_no) + blk_no;

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

int __aggr_mq_thread (void* arg)
{
	bdbm_drv_info_t* bdi = (bdbm_drv_info_t*)arg;
	uint64_t nr_punits = BDBM_GET_NR_PUNITS (bdi->parm_dev);
	uint64_t loop;

	if (aggr_rq == NULL || aggr_thread == NULL) {
		bdbm_msg ("invalid parameters (aggr_rq=%p, aggr_thread=%p",
			aggr_rq, aggr_thread);
		return 0;
	}

	for (;;) {
		/* give a chance to other processes if Q is empty */
		//if (bdbm_prior_queue_is_all_empty (aggr_rq)) {
		if (bdbm_queue_is_all_empty (aggr_rq)) {
			bdbm_thread_schedule_setup (aggr_thread);
			if (bdbm_queue_is_all_empty (aggr_rq)) {
			//if (bdbm_prior_queue_is_all_empty (aggr_rq)) {
				/* ok... go to sleep */
				if (bdbm_thread_schedule_sleep (aggr_thread) == SIGKILL)
					break;
			} else {
				/* there are items in Q; wake up */
				bdbm_thread_schedule_cancel (aggr_thread);
			}
		}

		//bdbm_sema_lock(&aggr_lock);
		/* send reqs until Q becomes empty */
		for (loop = 0; loop < nr_punits; loop++) {
			//bdbm_prior_queue_item_t* qitem = NULL;
			bdbm_llm_req_t* r = NULL;

			/* if pu is busy, then go to the next pnit */
			if (!bdbm_sema_try_lock (&bdbm_aggr_punit_locks[loop]))
				continue;

			//if ((r = (bdbm_llm_req_t*)bdbm_prior_queue_dequeue (aggr_rq, loop, &qitem)) == NULL) {
			if ((r = (bdbm_llm_req_t*)bdbm_queue_dequeue (aggr_rq, loop)) == NULL) {
				bdbm_sema_unlock (&bdbm_aggr_punit_locks[loop]);
				continue;
			}


			//tjkim
			//bdbm_msg("ag_deque punit: %llu, v: %d, lpa: %llu", r->phyaddr.punit_id, r->volume, r->logaddr.lpa[0]);

			//r->ptr_qitem = qitem;

			if (_bdbm_dm_inf.make_req(bdi, r)) {
				bdbm_sema_unlock (&bdbm_aggr_punit_locks[loop]);

				/* TODO: I do not check whether it works well or not */
				bdi->ptr_dm_inf->end_req (bdi, r);
				bdbm_warning ("oops! make_req failed");
			}
		}

		//bdbm_sema_unlock(&aggr_lock);
	}

	return 0;
}


int bdbm_aggr_init (bdbm_drv_info_t* bdi, uint8_t volume)
{
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	uint64_t nr_virt_blocks = np->nr_blocks_per_ssd * np->nr_volumes;
	uint64_t nr_punits = BDBM_GET_NR_PUNITS (bdi->parm_dev);
	uint64_t loop;


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

	if(cur_pblock == NULL) {
		if ((cur_pblock = (uint64_t*)bdbm_zmalloc(sizeof(uint64_t) * (np->nr_channels * np->nr_chips_per_channel))) == NULL) {
			bdbm_error ("bdbm_zmalloc failed");
			goto fail;
		}
		memset(cur_pblock, 0x00, sizeof(uint64_t) * (np->nr_channels * np->nr_chips_per_channel));
	}

	/* see if bdi is valid or not */
	if (bdi == NULL) {
		bdbm_warning ("bid is NULL");
		return 1;
	}

	// attach device related interface, such as dm_probe, dm_open, to bdi
	bdi->ptr_dm_inf = &_bdbm_aggr_inf;

	if (run_flag == FALSE) {
		// works should be done once
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

			/* create queue */
			//if ((aggr_rq = bdbm_prior_queue_create (nr_punits, INFINITE_QUEUE)) == NULL) {
			if ((aggr_rq = bdbm_queue_create (nr_punits, INFINITE_QUEUE)) == NULL) {
				bdbm_error ("bdbm_prior_queue_create failed");
				goto fail;
			}

			/* create completion locks for parallel units */
			if((bdbm_aggr_punit_locks = (bdbm_sema_t*)bdbm_malloc_atomic(sizeof(bdbm_sema_t) * nr_punits)) == NULL) {
				bdbm_error("bdbm_malloc_atomic failed");
				goto fail;
			}
			for (loop = 0; loop < nr_punits; loop++) {
				bdbm_sema_init (&bdbm_aggr_punit_locks[loop]);
			}

			/* create & run a thread */
			if ((aggr_thread = bdbm_thread_create (
							__aggr_mq_thread, bdi, "__aggr_mq_thread")) == NULL) {
				bdbm_error ("kthread_create failed");
				goto fail;
			}
			bdbm_thread_run (aggr_thread);
		}

		bdbm_sema_init(&aggr_lock);
#if defined(ENABLE_SEQ_DBG)
		bdbm_sema_init (dbg_seq);
#endif

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
	if (bdbm_aggr_punit_locks)
		bdbm_free_atomic (bdbm_aggr_punit_locks);
	if (aggr_rq)
		bdbm_queue_destroy (aggr_rq);
		//bdbm_prior_queue_destroy (aggr_rq);

	return 1;
}

void bdbm_aggr_exit (bdbm_drv_info_t* bdi)
{
	uint64_t loop;
	uint64_t nr_punits = BDBM_GET_NR_PUNITS (bdi->parm_dev);
	if(destroy_flag == FALSE) {
		destroy_flag = TRUE;

		if(bdbm_aggr_mapping != NULL) 
			bdbm_free(bdbm_aggr_mapping);

		if(bdbm_aggr_pblock_status != NULL)
			bdbm_free(bdbm_aggr_pblock_status);

		bdbm_sema_free(&aggr_lock);

		/* wait until Q becomes empty */
		//while (!bdbm_prior_queue_is_all_empty (aggr_rq)) {
		while (!bdbm_queue_is_all_empty (aggr_rq)) {
			//bdbm_msg ("llm items = %llu", bdbm_prior_queue_get_nr_items (aggr_rq));
			bdbm_msg ("llm items = %llu", bdbm_queue_get_nr_items (aggr_rq));
			bdbm_thread_msleep (1);
		}

		/* kill kthread */
		bdbm_thread_stop (aggr_thread);

		for (loop = 0; loop < nr_punits; loop++) {
			bdbm_sema_lock (&bdbm_aggr_punit_locks[loop]);
		}

		/* release all the relevant data structures */
		if (aggr_rq)
			//bdbm_prior_queue_destroy (aggr_rq);
			bdbm_queue_destroy (aggr_rq);

		if(bdbm_aggr_punit_locks != NULL)
			bdbm_free_atomic(bdbm_aggr_punit_locks);

	}
}

bdbm_dm_inf_t* bdbm_aggr_get_inf (bdbm_drv_info_t* bdi)
{
	return &_bdbm_aggr_inf;
}

uint32_t bdbm_aggr_allocate_blocks(bdbm_device_params_t *np, uint64_t channel_no, uint64_t chip_no, uint64_t block_no, uint32_t volume)
{
	uint32_t loop = 0;
	uint64_t aggr_idx, stat_idx;
	uint64_t punit_num;
	
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

	punit_num = channel_no * np->nr_chips_per_channel + chip_no;
	stat_idx = __get_aggr_pblock_idx(np, channel_no, chip_no, cur_pblock[punit_num]);
	bdbm_bug_on(stat_idx >= np->nr_blocks_per_ssd);

	while (bdbm_aggr_pblock_status[stat_idx] != AGGR_PBLOCK_FREE) {
		loop++;
		cur_pblock[punit_num] = cur_pblock[punit_num] + 1;
		if(cur_pblock[punit_num] == np->nr_blocks_per_chip) 
			cur_pblock[punit_num] = 0;
		// check for infinite loop
		if(loop >= np->nr_blocks_per_chip) {
			bdbm_error ("There is no free super block to allocate for volume %d", volume);
			return 1;
		}
		stat_idx = __get_aggr_pblock_idx(np, channel_no, chip_no, cur_pblock[punit_num]);
		bdbm_bug_on(stat_idx >= np->nr_blocks_per_ssd);
	}

	aggr_idx = __get_aggr_vblock_idx(np, volume, channel_no, chip_no, block_no);
	bdbm_bug_on(aggr_idx >= (np->nr_blocks_per_ssd * np->nr_volumes));
	bdbm_bug_on(bdbm_aggr_mapping[aggr_idx] != 0);
	bdbm_aggr_mapping[aggr_idx] = cur_pblock[punit_num];
	bdbm_aggr_pblock_status[stat_idx] = AGGR_PBLOCK_ALLOCATED;
	
	//bdbm_msg("aggr allocation volume: %d, pblock: %llu", volume, cur_pblock[punit_num]);
	return 0;
}

uint32_t bdbm_aggr_return_blocks(bdbm_device_params_t *np, uint64_t channel_no, uint64_t chip_no, uint64_t block_no, uint32_t volume) {
	uint64_t aggr_idx, tblk_num, stat_idx;

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

	aggr_idx = __get_aggr_vblock_idx(np, volume, channel_no, chip_no, block_no);
	bdbm_bug_on(aggr_idx >= (np->nr_blocks_per_ssd * np->nr_volumes));
	tblk_num = bdbm_aggr_mapping[aggr_idx];
	bdbm_aggr_mapping[aggr_idx] = 0;
	bdbm_bug_on(tblk_num >= np->nr_blocks_per_chip);
	stat_idx = __get_aggr_pblock_idx(np, channel_no, chip_no, tblk_num);
	bdbm_bug_on(stat_idx >= np->nr_blocks_per_ssd);
	bdbm_bug_on(bdbm_aggr_pblock_status[stat_idx] != AGGR_PBLOCK_ALLOCATED);
	bdbm_aggr_pblock_status[stat_idx] = AGGR_PBLOCK_FREE;

	return 0;
}

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
	uint64_t channel_no = ptr_llm_req->phyaddr.channel_no;
	uint64_t chip_no = ptr_llm_req->phyaddr.chip_no;
	uint64_t org_block_no = ptr_llm_req->phyaddr.block_no;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	uint64_t aggr_idx = __get_aggr_vblock_idx(np, volume, channel_no, chip_no, org_block_no);
	uint32_t ret;

	//while (bdbm_prior_queue_get_nr_items (aggr_rq) >= 96) {
	while (bdbm_queue_get_nr_items (aggr_rq) >= 96) {
		bdbm_thread_yield ();
	}

#if defined(ENABLE_SEQ_DBG)
	bdbm_sema_lock (&dbg_seq);
#endif

	// translate block number
	bdbm_bug_on (aggr_idx >= np->nr_blocks_per_ssd * np->nr_volumes);
	ptr_llm_req->phyaddr.block_no = bdbm_aggr_mapping[aggr_idx];

	//bdbm_msg("   enque punit: %llu, v: %d, lpa: %llu", ptr_llm_req->phyaddr.punit_id, volume, ptr_llm_req->logaddr.lpa[0]);
	/* put a request into Q */
	/*
	if ((ret = bdbm_prior_queue_enqueue (aggr_rq, ptr_llm_req->phyaddr.punit_id, 
					__convert_to_unique_lpa(ptr_llm_req->logaddr.lpa[0], volume), (void*)ptr_llm_req))) {
					//ptr_llm_req->logaddr.lpa[0], (void*)ptr_llm_req))) {
		bdbm_msg ("bdbm_prior_queue_enqueue failed");
	}
	*/

					
	if ((ret = bdbm_queue_enqueue (aggr_rq, ptr_llm_req->phyaddr.punit_id, (void*)ptr_llm_req))) {
		bdbm_msg ("bdbm_queue_enqueue failed");
	}

	/* wake up thread if it sleeps */
	bdbm_thread_wakeup (aggr_thread);

/*
	bdbm_msg("aggr make_req, volume: %d, vblock: %llu, pblock: %llu", 
			volume, org_block_no, bdbm_aggr_mapping[aggr_idx]);
*/
	//return _bdbm_dm_inf.make_req(bdi, ptr_llm_req);
	return 0;
}

uint32_t dm_aggr_make_reqs (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr){
	uint32_t volume = hr->volume;
	uint32_t i, ret;
	bdbm_llm_req_t* lr = NULL;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
#ifdef TIMELINE_DEBUG_TJKIM
	bdbm_stopwatch_t aggr_sw;
	bdbm_stopwatch_start(&aggr_sw);
#endif

	if(aggr_rq == NULL) {
		bdbm_msg("aggr_rq is NULL");
		return 1;
	}
#if defined(ENABLE_SEQ_DBG)
	bdbm_sema_lock (&dbg_seq);
#endif

	/* wait until there are enough free slots in Q */
	/*
	while (bdbm_prior_queue_get_nr_items (aggr_rq) >= 96) {
	//while (bdbm_queue_get_nr_items (aggr_rq) >= 96) {
		bdbm_thread_yield ();
	}
	*/

	// translate block number
	bdbm_hlm_for_each_llm_req (lr, hr, i) {
		uint64_t channel_no = lr->phyaddr.channel_no;
		uint64_t chip_no = lr->phyaddr.chip_no;
		uint64_t org_block_no = lr->phyaddr.block_no;
		uint64_t aggr_idx = __get_aggr_vblock_idx(np, volume, channel_no, chip_no, org_block_no);
		bdbm_bug_on (aggr_idx >= np->nr_blocks_per_ssd * np->nr_volumes);
		lr->phyaddr.block_no = bdbm_aggr_mapping[aggr_idx];

		//bdbm_msg("  enques punit: %llu, v: %d, lpa: %llu", lr->phyaddr.punit_id, volume, lr->logaddr.lpa[0]);

		/* put a request into Q */
		/*
		if ((ret = bdbm_prior_queue_enqueue (aggr_rq, lr->phyaddr.punit_id, 
						__convert_to_unique_lpa(lr->logaddr.lpa[0], volume), (void*)lr))) {
			//lr->logaddr.lpa[0], (void*)lr))) {
			bdbm_msg ("bdbm_prior_queue_enqueue failed");
		}
		*/

		if ((ret = bdbm_queue_enqueue (aggr_rq, lr->phyaddr.punit_id, (void*)lr))){
			bdbm_msg ("bdbm_queue_enqueue failed");
		}

		/* wake up thread if it sleeps */
		if(aggr_thread == NULL) {
			bdbm_msg("aggr_thread is NULL");
			return 1;
		}
		bdbm_thread_wakeup (aggr_thread);
	}

#ifdef TIMELINE_DEBUG_TJKIM
	bdbm_msg("volume: %d, aggr elapsed time: %llu", volume, bdbm_stopwatch_get_elapsed_time_us(&aggr_sw));
#endif
	//return _bdbm_dm_inf.make_reqs(bdi, hr);
	return 0;
}

void dm_aggr_end_req (bdbm_drv_info_t* bdi, bdbm_llm_req_t* ptr_llm_req) {
	//bdbm_prior_queue_item_t* qitem = (bdbm_prior_queue_item_t*)ptr_llm_req->ptr_qitem;

	//bdbm_sema_lock(&aggr_lock);

#ifdef TIMELINE_DEBUG_TJKIM
	bdbm_msg("dm_aggr_end_req");
#endif

	//bdbm_prior_queue_remove (aggr_rq, qitem);

	/*
	bdbm_msg(" end_req punit: %llu, v: %d,  lpa: %llu",
			ptr_llm_req->phyaddr.punit_id, ptr_llm_req->volume, ptr_llm_req->logaddr.lpa[0]);
	*/

	/* complete a lock */
	bdbm_sema_unlock (&bdbm_aggr_punit_locks[ptr_llm_req->phyaddr.punit_id]);

	//bdbm_sema_unlock(&aggr_lock);
	/* update the elapsed time taken by NAND devices */

	bdi->ptr_llm_inf->end_req(bdi, ptr_llm_req);
	//return _bdbm_dm_inf.end_req(bdi, ptr_llm_req);
#if defined(ENABLE_SEQ_DBG)
	bdbm_sema_unlock (&dbg_seq);
#endif


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

uint64_t bdbm_aggr_get_nr_queue_items(void) {
	//return bdbm_prior_queue_get_nr_items(aggr_rq);
	return bdbm_queue_get_nr_items(aggr_rq);
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
EXPORT_SYMBOL (bdbm_aggr_get_nr_queue_items);

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("RISA Device Wrapper");
MODULE_LICENSE ("GPL");

module_init (risa_dev_init);
module_exit (risa_dev_exit);
#endif
