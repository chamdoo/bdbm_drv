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
#include <linux/slab.h>
#include <linux/interrupt.h>

#elif defined (USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "debug.h"
#include "platform.h"
#include "bdbm_drv.h"
#include "dev_ramssd.h"
#include "utils/ufile.h"

/* Functions for Managing DRAM SSD */
static uint8_t* __ramssd_page_addr (
	struct dev_ramssd_info* ptr_ramssd_info,
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no,
	uint64_t page_no)
{
	uint8_t* ptr_ramssd = NULL;
	uint64_t ramssd_addr = 0;

	/* calculate the address offset */
	ramssd_addr += dev_ramssd_get_channel_size (ptr_ramssd_info) * channel_no;
	ramssd_addr += dev_ramssd_get_chip_size (ptr_ramssd_info) * chip_no;
	ramssd_addr += dev_ramssd_get_block_size (ptr_ramssd_info) * block_no;
	ramssd_addr += dev_ramssd_get_page_size (ptr_ramssd_info) * page_no;

	/* get the address */
	ptr_ramssd = (uint8_t*)(ptr_ramssd_info->ptr_ssdram) + ramssd_addr;

	return ptr_ramssd;
}

static uint8_t* __ramssd_block_addr (
	struct dev_ramssd_info* ptr_ramssd_info,
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no)
{
	uint8_t* ptr_ramssd = NULL;
	uint64_t ramssd_addr = 0;

	/* calculate the address offset */
	ramssd_addr += dev_ramssd_get_channel_size (ptr_ramssd_info) * channel_no;
	ramssd_addr += dev_ramssd_get_chip_size (ptr_ramssd_info) * chip_no;
	ramssd_addr += dev_ramssd_get_block_size (ptr_ramssd_info) * block_no;

	/* get the address */
	ptr_ramssd = (uint8_t*)(ptr_ramssd_info->ptr_ssdram) + ramssd_addr;

	return ptr_ramssd;
}

static void* __ramssd_alloc_ssdram (struct nand_params* ptr_nand_params)
{
	void* ptr_ramssd = NULL;
	uint64_t page_size_in_bytes;
	uint64_t nr_pages_in_ssd;
	uint64_t ssd_size_in_bytes;

	page_size_in_bytes = 
		ptr_nand_params->page_main_size + 
		ptr_nand_params->page_oob_size;

	nr_pages_in_ssd =
		ptr_nand_params->nr_channels *
		ptr_nand_params->nr_chips_per_channel *
		ptr_nand_params->nr_blocks_per_chip *
		ptr_nand_params->nr_pages_per_block;

	ssd_size_in_bytes = 
		nr_pages_in_ssd * 
		page_size_in_bytes;

	bdbm_msg ("=====================================================================");
	bdbm_msg ("RAM DISK INFO");
	bdbm_msg ("=====================================================================");

	bdbm_msg ("page size (bytes) = %llu (%llu + %llu)", 
		page_size_in_bytes, 
		ptr_nand_params->page_main_size, 
		ptr_nand_params->page_oob_size);

	bdbm_msg ("# of pages in the SSD = %llu", nr_pages_in_ssd);

	bdbm_msg ("the SSD capacity: %llu (B), %llu (KB), %llu (MB)",
		ptr_nand_params->device_capacity_in_byte,
		BDBM_SIZE_KB(ptr_nand_params->device_capacity_in_byte),
		BDBM_SIZE_MB(ptr_nand_params->device_capacity_in_byte));


	/* allocate the memory for the SSD */
	if ((ptr_ramssd = (void*)bdbm_malloc
			(ssd_size_in_bytes * sizeof (uint8_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		return NULL;
	}
	bdbm_memset ((uint8_t*)ptr_ramssd, 0xFF, ssd_size_in_bytes * sizeof (uint8_t));

	bdbm_msg ("ramssd addr = %p", ptr_ramssd);
	bdbm_msg ("");

	/* good; return ramssd addr */
	return (void*)ptr_ramssd;
}

static void __ramssd_free_ssdram (void* ptr_ramssd) 
{
	bdbm_free (ptr_ramssd);
}

static uint8_t __ramssd_read_page (
	struct dev_ramssd_info* ptr_ramssd_info, 
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no,
	uint64_t page_no,
	uint8_t* kpg_flags,
	uint8_t** ptr_page_data,
	uint8_t* ptr_oob_data,
	uint8_t oob,
	uint8_t partial)
{
	uint8_t ret = 0;
	uint8_t* ptr_ramssd_addr = NULL;
	uint32_t nr_pages, loop;

	/* get the memory address for the destined page */
	if ((ptr_ramssd_addr = __ramssd_page_addr (
			ptr_ramssd_info, channel_no, chip_no, block_no, page_no)) == NULL) {
		bdbm_error ("invalid ram_addr (%p)", ptr_ramssd_addr);
		ret = 1;
		goto fail;
	}

	/* for better performance, RAMSSD directly copies the SSD data to kernel pages */
	nr_pages = ptr_ramssd_info->nand_params->page_main_size / KERNEL_PAGE_SIZE;
	if (ptr_ramssd_info->nand_params->page_main_size % KERNEL_PAGE_SIZE != 0) {
		bdbm_error ("The page-cache granularity (%lu) is not matched to the flash page size (%llu)", 
			KERNEL_PAGE_SIZE, ptr_ramssd_info->nand_params->page_main_size);
		ret = 1;
		goto fail;
	}

	/* copy the main page data to a buffer */
	for (loop = 0; loop < nr_pages; loop++) {
		if (partial == 1 && kpg_flags[loop] == MEMFLAG_KMAP_PAGE) {
			continue;
		}
		if (kpg_flags != NULL && (kpg_flags[loop] & MEMFLAG_DONE) == MEMFLAG_DONE) {
			/* it would be possible that part of the page was already read at the level of the cache */
			continue;
		}

		bdbm_memcpy (
			ptr_page_data[loop], 
			ptr_ramssd_addr + KERNEL_PAGE_SIZE * loop, 
			KERNEL_PAGE_SIZE
		);
	}

	/* copy the OOB data to a buffer */
	if (partial == 0 && oob && ptr_oob_data != NULL) {
		bdbm_memcpy (
			ptr_oob_data, 
			ptr_ramssd_addr + ptr_ramssd_info->nand_params->page_main_size,
			ptr_ramssd_info->nand_params->page_oob_size
		);
	}

fail:
	return ret;
}

static uint8_t __ramssd_prog_page (
	struct dev_ramssd_info* ptr_ramssd_info, 
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no,
	uint64_t page_no,
	uint8_t* kpg_flags,
	uint8_t** ptr_page_data,
	uint8_t* ptr_oob_data,
	uint8_t oob)
{
	uint8_t ret = 0;
	uint8_t* ptr_ramssd_addr = NULL;
	uint32_t nr_pages, loop;

	/* get the memory address for the destined page */
	if ((ptr_ramssd_addr = __ramssd_page_addr (
			ptr_ramssd_info, channel_no, chip_no, block_no, page_no)) == NULL) {
		bdbm_error ("invalid ram addr (%p)", ptr_ramssd_addr);
		ret = 1;
		goto fail;
	}

	/* for better performance, RAMSSD directly copies the SSD data to pages */
	nr_pages = ptr_ramssd_info->nand_params->page_main_size / KERNEL_PAGE_SIZE;
	if (ptr_ramssd_info->nand_params->page_main_size % KERNEL_PAGE_SIZE != 0) {
		bdbm_error ("The page-cache granularity (%lu) is not matched to the flash page size (%llu)", 
			KERNEL_PAGE_SIZE, ptr_ramssd_info->nand_params->page_main_size);
		ret = 1;
		goto fail;
	}

	/* copy the main page data to a buffer */
	for (loop = 0; loop < nr_pages; loop++) {
		bdbm_memcpy (
			ptr_ramssd_addr + KERNEL_PAGE_SIZE * loop, 
			ptr_page_data[loop], 
			KERNEL_PAGE_SIZE
		);
	}

	/* copy the OOB data to a buffer */
	if (oob && ptr_oob_data != NULL) {
		bdbm_memcpy (
			ptr_ramssd_addr + ptr_ramssd_info->nand_params->page_main_size,
			ptr_oob_data,
			ptr_ramssd_info->nand_params->page_oob_size
		);
		/*
		bdbm_msg ("lpa: %llu %llu", 
			((uint64_t*)(ptr_ramssd_addr + ptr_ramssd_info->nand_params->page_main_size))[0],
			((uint64_t*)ptr_oob_data)[0]);
		*/
	}

fail:
	return ret;
}

static uint8_t __ramssd_erase_block (
	struct dev_ramssd_info* ptr_ramssd_info, 
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no)
{
	uint8_t* ptr_ram_addr = NULL;

	/* get the memory address for the destined block */
	if ((ptr_ram_addr = __ramssd_block_addr 
			(ptr_ramssd_info, channel_no, chip_no, block_no)) == NULL) {
		bdbm_error ("invalid ssdram addr (%p)", ptr_ram_addr);
		return 1;
	}

	/* erase the block (set all the values to '1') */
	memset (ptr_ram_addr, 0xFF, dev_ramssd_get_block_size (ptr_ramssd_info));

	return 0;
}

static uint32_t __ramssd_send_cmd (
	struct dev_ramssd_info* ptr_ramssd_info, struct bdbm_llm_req_t* ptr_req)
{
	uint8_t ret = 0;
	uint8_t use_oob = 1;	/* read or program OOB by default; why not??? */
	uint8_t use_partial = 0;

	if (ptr_ramssd_info->nand_params->page_oob_size == 0)
		use_oob = 0;

	switch (ptr_req->req_type) {
	case REQTYPE_RMW_READ:
		use_partial = 1;
	case REQTYPE_READ:
	case REQTYPE_GC_READ:
		ret = __ramssd_read_page (
			ptr_ramssd_info, 
			ptr_req->phyaddr->channel_no, 
			ptr_req->phyaddr->chip_no, 
			ptr_req->phyaddr->block_no, 
			ptr_req->phyaddr->page_no, 
			ptr_req->kpg_flags,
			ptr_req->pptr_kpgs,
			ptr_req->ptr_oob,
			use_oob,
			use_partial);
		break;

	case REQTYPE_WRITE:
	case REQTYPE_GC_WRITE:
	case REQTYPE_RMW_WRITE:
		ret = __ramssd_prog_page (
			ptr_ramssd_info, 
			ptr_req->phyaddr->channel_no,
			ptr_req->phyaddr->chip_no,
			ptr_req->phyaddr->block_no,
			ptr_req->phyaddr->page_no,
			ptr_req->kpg_flags,
			ptr_req->pptr_kpgs,
			ptr_req->ptr_oob,
			use_oob);
		break;

	case REQTYPE_GC_ERASE:
		ret = __ramssd_erase_block (
			ptr_ramssd_info, 
			ptr_req->phyaddr->channel_no, 
			ptr_req->phyaddr->chip_no, 
			ptr_req->phyaddr->block_no);
		break;

	case REQTYPE_READ_DUMMY:
		/* do nothing for READ_DUMMY */
		ret = 0;
		break;

	case REQTYPE_TRIM:
		/* do nothing for TRIM */
		ret = 0;
		break;

	default:
		bdbm_error ("invalid command");
		ret = 1;
		break;
	}

	return ret;
}

void __ramssd_cmd_done (struct dev_ramssd_info* ptr_ramssd_info)
{
	uint64_t loop, nr_parallel_units;

	nr_parallel_units = dev_ramssd_get_chips_per_ssd (ptr_ramssd_info);

	for (loop = 0; loop < nr_parallel_units; loop++) {
		unsigned long flags;

		bdbm_spin_lock_irqsave (&ptr_ramssd_info->ramssd_lock, flags);
		if (ptr_ramssd_info->ptr_punits[loop].ptr_req != NULL) {
			struct dev_ramssd_punit* punit;
			int64_t elapsed_time_in_us;

			punit = &ptr_ramssd_info->ptr_punits[loop];
			elapsed_time_in_us = bdbm_stopwatch_get_elapsed_time_us (&punit->sw);

			if (elapsed_time_in_us >= punit->target_elapsed_time_us) {
				void* ptr_req = punit->ptr_req;
				punit->ptr_req = NULL;
				bdbm_spin_unlock_irqrestore (&ptr_ramssd_info->ramssd_lock, flags);

				/* call the interrupt handler */
				ptr_ramssd_info->intr_handler (ptr_req);
			} else {
				bdbm_spin_unlock_irqrestore (&ptr_ramssd_info->ramssd_lock, flags);
			}
		} else {
			bdbm_spin_unlock_irqrestore (&ptr_ramssd_info->ramssd_lock, flags);
		}
	}
}


/* Functions for Timing Management */
static void __ramssd_timing_cmd_done (unsigned long arg)
{
	/* forward it to ramssd_cmd_done */
	__ramssd_cmd_done ((struct dev_ramssd_info*)arg);
}

#if defined (KERNEL_MODE)
static enum hrtimer_restart __ramssd_timing_hrtimer_cmd_done (struct hrtimer *ptr_hrtimer)
{
	ktime_t ktime;
	struct dev_ramssd_info* ptr_ramssd_info;
	
	ptr_ramssd_info = (struct dev_ramssd_info*)container_of 
		(ptr_hrtimer, struct dev_ramssd_info, hrtimer);

	/* call a tasklet */
	tasklet_schedule (ptr_ramssd_info->tasklet); 

	ktime = ktime_set (0, 50 * 1000);
	hrtimer_start (&ptr_ramssd_info->hrtimer, ktime, HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}
#endif

uint32_t __ramssd_timing_register_schedule (struct dev_ramssd_info* ptr_ramssd_info)
{
	switch (ptr_ramssd_info->emul_mode) {
	case DEVICE_TYPE_RAMDRIVE:
	case DEVICE_TYPE_USER_RAMDRIVE:
		__ramssd_cmd_done (ptr_ramssd_info);
		break;
#if defined (KERNEL_MODE)
	case DEVICE_TYPE_RAMDRIVE_INTR:
		/*__ramssd_cmd_done (ptr_ramssd_info);*/
		break;
	case DEVICE_TYPE_RAMDRIVE_TIMING:
		tasklet_schedule (ptr_ramssd_info->tasklet); 
		break;
#endif
	default:
		__ramssd_timing_cmd_done ((unsigned long)ptr_ramssd_info);
		break;
	}

	return 0;
}

uint32_t __ramssd_timing_create (struct dev_ramssd_info* ptr_ramssd_info) 
{
	uint32_t ret = 0;

	switch (ptr_ramssd_info->emul_mode) {
	case DEVICE_TYPE_RAMDRIVE:
	case DEVICE_TYPE_USER_RAMDRIVE:
		bdbm_msg ("use TIMING_DISABLE mode!");
		break;
#if defined (KERNEL_MODE)
	case DEVICE_TYPE_RAMDRIVE_INTR: 
		{
			ktime_t ktime;
			bdbm_msg ("HRTIMER is created!");
			hrtimer_init (&ptr_ramssd_info->hrtimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
			ptr_ramssd_info->hrtimer.function = __ramssd_timing_hrtimer_cmd_done;
			ktime = ktime_set (0, 500 * 1000);
			hrtimer_start (&ptr_ramssd_info->hrtimer, ktime, HRTIMER_MODE_REL);
		}
		/* no break! we initialize a tasklet together */
	case DEVICE_TYPE_RAMDRIVE_TIMING: 
		bdbm_msg ("TASKLET is created!");
		if ((ptr_ramssd_info->tasklet = (struct tasklet_struct*)
				bdbm_malloc_atomic (sizeof (struct tasklet_struct))) == NULL) {
			bdbm_msg ("bdbm_malloc_atomic failed");
			ret = 1;
		} else {
			tasklet_init (ptr_ramssd_info->tasklet, 
				__ramssd_timing_cmd_done, (unsigned long)ptr_ramssd_info);
		}
		break;
#endif
	default:
		bdbm_error ("invalid timing mode");
		ret = 1;
		break;
	}

	return ret;
}

void __ramssd_timing_destory (struct dev_ramssd_info* ptr_ramssd_info)
{
	switch (ptr_ramssd_info->emul_mode) {
	case DEVICE_TYPE_RAMDRIVE:
	case DEVICE_TYPE_USER_RAMDRIVE:
		bdbm_msg ("TIMING_DISABLE is done!");
		break;
#if defined (KERNEL_MODE)
	case DEVICE_TYPE_RAMDRIVE_INTR:
		bdbm_msg ("HRTIMER is canceled");
		hrtimer_cancel (&ptr_ramssd_info->hrtimer);
		/* no break! we destroy a tasklet */
	case DEVICE_TYPE_RAMDRIVE_TIMING:
		bdbm_msg ("TASKLET is killed");
		tasklet_kill (ptr_ramssd_info->tasklet);
		break;
#endif
	default:
		break;
	}
}

/* Functions Exposed to External Files */
struct dev_ramssd_info* dev_ramssd_create (
	struct nand_params* ptr_nand_params, 
	void (*intr_handler)(void*))
{
	uint64_t loop, nr_parallel_units;
	struct dev_ramssd_info* ptr_ramssd_info = NULL;

	/* create a ramssd info */
	if ((ptr_ramssd_info = (struct dev_ramssd_info*)
			bdbm_malloc_atomic (sizeof (struct dev_ramssd_info))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail;
	}

	/* seup parameters */
	ptr_ramssd_info->intr_handler = intr_handler;
	ptr_ramssd_info->emul_mode = ptr_nand_params->device_type;
	ptr_ramssd_info->nand_params = ptr_nand_params;

	/* allocate ssdram space */
	if ((ptr_ramssd_info->ptr_ssdram = 
			__ramssd_alloc_ssdram (ptr_ramssd_info->nand_params)) == NULL) {
		bdbm_error ("__ramssd_alloc_ssdram failed");
		goto fail_ssdram;
	}

	/* create parallel units */
	nr_parallel_units = dev_ramssd_get_chips_per_ssd (ptr_ramssd_info);

	if ((ptr_ramssd_info->ptr_punits = (struct dev_ramssd_punit*)
			bdbm_malloc_atomic (sizeof (struct dev_ramssd_punit) * nr_parallel_units)) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail_punits;
	}
	for (loop = 0; loop < nr_parallel_units; loop++) {
		ptr_ramssd_info->ptr_punits[loop].ptr_req = NULL;
	}

	/* create and register a tasklet */
	if (__ramssd_timing_create (ptr_ramssd_info) != 0) {
		bdbm_msg ("dev_ramssd_timing_create failed");
		goto fail_timing;
	}

	/* create spin_lock */
	bdbm_spin_lock_init (&ptr_ramssd_info->ramssd_lock);

	/* done */
	ptr_ramssd_info->is_init = 1;

	return ptr_ramssd_info;

fail_timing:
	bdbm_free_atomic (ptr_ramssd_info->ptr_punits);

fail_punits:
	__ramssd_free_ssdram (ptr_ramssd_info->ptr_ssdram);

fail_ssdram:
	bdbm_free_atomic (ptr_ramssd_info);

fail:
	return NULL;
}

void dev_ramssd_destroy (struct dev_ramssd_info* ptr_ramssd_info)
{
	/* kill tasklet */
	__ramssd_timing_destory (ptr_ramssd_info);

	/* free ssdram */
	__ramssd_free_ssdram (ptr_ramssd_info->ptr_ssdram);

	/* release other stuff */
	bdbm_free_atomic (ptr_ramssd_info->ptr_punits);
	bdbm_free_atomic (ptr_ramssd_info);
}

uint32_t dev_ramssd_send_cmd (struct dev_ramssd_info* ptr_ramssd_info, struct bdbm_llm_req_t* llm_req)
{
	uint32_t ret;

	if ((ret = __ramssd_send_cmd (ptr_ramssd_info, llm_req)) == 0) {
		unsigned long flags;
		int64_t target_elapsed_time_us = 0;
		uint64_t punit_id;

		/* get the punit_id */
		punit_id = dev_ramssd_get_chips_per_channel (ptr_ramssd_info) * 
			llm_req->phyaddr->channel_no + 
			llm_req->phyaddr->chip_no;

		/* get the target elapsed time depending on the type of req */
		if (ptr_ramssd_info->emul_mode == DEVICE_TYPE_RAMDRIVE_INTR) {
			switch (llm_req->req_type) {
			case REQTYPE_WRITE:
			case REQTYPE_GC_WRITE:
			case REQTYPE_RMW_WRITE:
				target_elapsed_time_us = ptr_ramssd_info->nand_params->page_prog_time_us;
				break;
			case REQTYPE_READ:
			case REQTYPE_GC_READ:
			case REQTYPE_RMW_READ:
				target_elapsed_time_us = ptr_ramssd_info->nand_params->page_read_time_us;
				break;
			case REQTYPE_GC_ERASE:
				target_elapsed_time_us = ptr_ramssd_info->nand_params->block_erase_time_us;
				break;
			case REQTYPE_READ_DUMMY:
				target_elapsed_time_us = 0;	/* dummy read */
				break;
			default:
				bdbm_error ("invalid REQTYPE (%u)", llm_req->req_type);
				bdbm_bug_on (1);
				break;
			}
			if (target_elapsed_time_us > 0) {
				target_elapsed_time_us -= (target_elapsed_time_us / 10);
			}
		} else {
			target_elapsed_time_us = 0;
		}

		/* register reqs */
		bdbm_spin_lock_irqsave (&ptr_ramssd_info->ramssd_lock, flags);
		if (ptr_ramssd_info->ptr_punits[punit_id].ptr_req == NULL) {
			ptr_ramssd_info->ptr_punits[punit_id].ptr_req = (void*)llm_req;
			/*ptr_ramssd_info->ptr_punits[punit_id].sw = bdbm_stopwatch_start ();*/
			bdbm_stopwatch_start (&ptr_ramssd_info->ptr_punits[punit_id].sw);
			ptr_ramssd_info->ptr_punits[punit_id].target_elapsed_time_us = target_elapsed_time_us;
		} else {
			bdbm_error ("More than two requests are assigned to the same parallel unit (ptr=%p, punit=%llu, lpa=%llu)",
				ptr_ramssd_info->ptr_punits[punit_id].ptr_req,
				punit_id,
				llm_req->lpa);
			bdbm_spin_unlock_irqrestore (&ptr_ramssd_info->ramssd_lock, flags);
			ret = 1;
			goto fail;
		}
		bdbm_spin_unlock_irqrestore (&ptr_ramssd_info->ramssd_lock, flags);

		/* register reqs for callback */
		__ramssd_timing_register_schedule (ptr_ramssd_info);
	}

fail:
	return ret;
}

void dev_ramssd_summary (struct dev_ramssd_info* ptr_ramssd_info)
{
	if (ptr_ramssd_info->is_init == 0) {
		bdbm_msg ("RAMSSD is not initialized yet");
		return;
	}

	bdbm_msg ("* A summary of the RAMSSD organization *");
	bdbm_msg (" - Total SSD size: %llu B (%llu MB)", dev_ramssd_get_ssd_size (ptr_ramssd_info), BDBM_SIZE_MB (dev_ramssd_get_ssd_size (ptr_ramssd_info)));
	bdbm_msg (" - Flash chip size: %llu", dev_ramssd_get_chip_size (ptr_ramssd_info));
	bdbm_msg (" - Flash block size: %llu", dev_ramssd_get_block_size (ptr_ramssd_info));
	bdbm_msg (" - Flash page size: %llu (main: %llu + oob: %llu)", dev_ramssd_get_page_size (ptr_ramssd_info), dev_ramssd_get_page_size_main (ptr_ramssd_info), dev_ramssd_get_page_size_oob (ptr_ramssd_info));
	bdbm_msg ("");
	bdbm_msg (" - # of pages per block: %llu", dev_ramssd_get_pages_per_block (ptr_ramssd_info));
	bdbm_msg (" - # of blocks per chip: %llu", dev_ramssd_get_blocks_per_chips (ptr_ramssd_info));
	bdbm_msg (" - # of chips per channel: %llu", dev_ramssd_get_chips_per_channel (ptr_ramssd_info));
	bdbm_msg (" - # of channels: %llu", dev_ramssd_get_channles_per_ssd (ptr_ramssd_info));
	bdbm_msg ("");
	bdbm_msg (" - kernel page size: %lu", KERNEL_PAGE_SIZE);
	bdbm_msg ("");
}

/* for snapshot */
uint32_t dev_ramssd_load (struct dev_ramssd_info* ptr_ramssd_info, const char* fn)
{
	/*struct file* fp = NULL;*/
	bdbm_file_t fp = 0;
	uint64_t len = 0;

	bdbm_msg ("dev_ramssd_load - begin");

	if (ptr_ramssd_info->ptr_ssdram == NULL) {
		bdbm_error ("ptr_ssdram is NULL");
		return 1;
	}
	
	if ((fp = bdbm_fopen (fn, O_RDWR, 0777)) == 0) {
		bdbm_error ("bdbm_fopen failed");
		return 1;
	}

	bdbm_msg ("dev_ramssd_load: DRAM read starts = %llu", len);
	len = dev_ramssd_get_ssd_size (ptr_ramssd_info);
	len = bdbm_fread (fp, 0, (uint8_t*)ptr_ramssd_info->ptr_ssdram, len);
	bdbm_msg ("dev_ramssd_load: DRAM read ends = %llu", len);

	bdbm_fclose (fp);

	bdbm_msg ("dev_ramssd_load - done");

	return 0;
}

uint32_t dev_ramssd_store (struct dev_ramssd_info* ptr_ramssd_info, const char* fn)
{
	/*struct file* fp = NULL;*/
	bdbm_file_t fp = 0;
	uint64_t pos = 0;
	uint64_t len = 0;

	bdbm_msg ("dev_ramssd_store - begin");

	if (ptr_ramssd_info->ptr_ssdram == NULL) {
		bdbm_error ("ptr_ssdram is NULL");
		return 1;
	}
	
	if ((fp = bdbm_fopen (fn, O_CREAT | O_WRONLY, 0777)) == 0) {
		bdbm_error ("bdbm_fopen failed");
		return 1;
	}

	len = dev_ramssd_get_ssd_size (ptr_ramssd_info);
	bdbm_msg ("dev_ramssd_store: DRAM store starts = %llu", len);
	while (pos < len) {
		pos += bdbm_fwrite (fp, pos, (uint8_t*)ptr_ramssd_info->ptr_ssdram + pos, len - pos);
	}
	bdbm_fsync (fp);
	bdbm_fclose (fp);

	bdbm_msg ("dev_ramssd_store: DRAM store ends = %llu", pos);
	bdbm_msg ("dev_ramssd_store - end");

	return 0;
}

