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
#include "ufile.h"
#include "dev_ramssd.h"

/*#define DBG_RMW*/
#define DATA_CHECK

#if defined (DATA_CHECK)
static void* __ptr_ramssd_data = NULL;
static uint8_t* __get_ramssd_data_addr (uint64_t lpa)
{
	uint64_t ramssd_addr = KPAGE_SIZE * lpa;
	return ((uint8_t*)__ptr_ramssd_data) + ramssd_addr;
}
#endif

/* Functions for Managing DRAM SSD */
static uint8_t* __ramssd_page_addr (
	dev_ramssd_info_t* ri,
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no,
	uint64_t page_no)
{
	uint8_t* ptr_ramssd = NULL;
	uint64_t ramssd_addr = 0;

	/* calculate the address offset */
	ramssd_addr += dev_ramssd_get_channel_size (ri) * channel_no;
	ramssd_addr += dev_ramssd_get_chip_size (ri) * chip_no;
	ramssd_addr += dev_ramssd_get_block_size (ri) * block_no;
	ramssd_addr += dev_ramssd_get_page_size (ri) * page_no;

	/* get the address */
	ptr_ramssd = (uint8_t*)(ri->ptr_ssdram) + ramssd_addr;

	return ptr_ramssd;
}

static uint8_t* __ramssd_block_addr (
	dev_ramssd_info_t* ri,
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no)
{
	uint8_t* ptr_ramssd = NULL;
	uint64_t ramssd_addr = 0;

	/* calculate the address offset */
	ramssd_addr += dev_ramssd_get_channel_size (ri) * channel_no;
	ramssd_addr += dev_ramssd_get_chip_size (ri) * chip_no;
	ramssd_addr += dev_ramssd_get_block_size (ri) * block_no;

	/* get the address */
	ptr_ramssd = (uint8_t*)(ri->ptr_ssdram) + ramssd_addr;

	return ptr_ramssd;
}

static void* __ramssd_alloc_ssdram (bdbm_device_params_t* ptr_nand_params)
{
	void* ptr_ramssd = NULL;
	uint64_t page_size_in_bytes;
	uint64_t nr_subpages_in_ssd;
	uint64_t ssd_size_in_bytes;

	page_size_in_bytes = 
		ptr_nand_params->page_main_size + 
		ptr_nand_params->page_oob_size;

	nr_subpages_in_ssd =
		ptr_nand_params->nr_channels *
		ptr_nand_params->nr_chips_per_channel *
		ptr_nand_params->nr_blocks_per_chip *
		ptr_nand_params->nr_subpages_per_block;

	ssd_size_in_bytes = 
		nr_subpages_in_ssd * 
		page_size_in_bytes;

	bdbm_msg ("=====================================================================");
	bdbm_msg ("RAM DISK INFO");
	bdbm_msg ("=====================================================================");
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

#if defined (DATA_CHECK)
	bdbm_msg ("*** building ptr_ramssd_data begins for data curruption checks...");
	if ((__ptr_ramssd_data = (void*)bdbm_malloc
			(ssd_size_in_bytes * sizeof (uint8_t))) == NULL) {
		bdbm_warning ("bdbm_malloc () failed for ptr_ramssd_data");
	}
	bdbm_memset ((uint8_t*)__ptr_ramssd_data, 0xFF, ssd_size_in_bytes * sizeof (uint8_t));
	bdbm_msg ("*** building ptr_ramssd_data done");
#endif

	/* good; return ramssd addr */
	return (void*)ptr_ramssd;
}

static void __ramssd_free_ssdram (void* ptr_ramssd) 
{
#if defined (DATA_CHECK)
	if (__ptr_ramssd_data) {
		bdbm_free (__ptr_ramssd_data);
	}
#endif
	bdbm_free (ptr_ramssd);
}

static uint8_t __ramssd_read_page (
	dev_ramssd_info_t* ri, 
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no,
	uint64_t page_no,
	kp_stt_t* kpg_flags,
	uint8_t** ptr_page_data,
	uint8_t* ptr_oob_data,
	uint8_t oob,
	uint8_t partial)
{
	uint8_t ret = 0;
	uint8_t* ptr_ramssd_addr = NULL;
	uint32_t nr_subpages, loop;

	/* get the memory address for the destined page */
	if ((ptr_ramssd_addr = __ramssd_page_addr (
			ri, channel_no, chip_no, block_no, page_no)) == NULL) {
		bdbm_error ("invalid ram_addr (%p)", ptr_ramssd_addr);
		ret = 1;
		goto fail;
	}

	/* for better performance, RAMSSD directly copies the SSD data to kernel pages */
	nr_subpages = ri->nand_params->page_main_size / KERNEL_PAGE_SIZE;
	if (ri->nand_params->page_main_size % KERNEL_PAGE_SIZE != 0) {
		bdbm_error ("The page-cache granularity (%lu) is not matched to the flash page size (%llu)", 
			KERNEL_PAGE_SIZE, ri->nand_params->page_main_size);
		ret = 1;
		goto fail;
	}

	/* copy the main page data to a buffer */
	for (loop = 0; loop < nr_subpages; loop++) {
		if (partial == 1 && kpg_flags[loop] == KP_STT_DATA) {
			continue;
		}

		if (kpg_flags != NULL && (kpg_flags[loop] & KP_STT_DONE) == KP_STT_DONE) {
			/* it would be possible that part of the page was already read at the level of the cache */
			continue;
		}

#ifdef DBG_RMW
		if (partial == 1) {
			bdbm_msg ("DEV-RMW_READ: lpa=%llu offset=%llu (%llu %llu %llu %llu)", 
				((uint64_t*)ptr_oob_data)[loop],
				loop, 
				channel_no, 
				chip_no, 
				block_no, 
				page_no);
		}
#endif

		bdbm_memcpy (
			ptr_page_data[loop], 
			ptr_ramssd_addr + KERNEL_PAGE_SIZE * loop, 
			KERNEL_PAGE_SIZE
		);
	}

	/* copy the OOB data to a buffer */
	if (partial == 0 && oob && ptr_oob_data != NULL) {
		bdbm_memcpy (ptr_oob_data, 
			ptr_ramssd_addr + ri->nand_params->page_main_size,
			ri->nand_params->page_oob_size
		);
	}

#if defined (DATA_CHECK)
	for (loop = 0; loop < nr_subpages; loop++) {
		uint64_t lpa = ((uint64_t*)ptr_oob_data)[loop];
		uint8_t* ptr_data_org = NULL;
		int pos;
		if (lpa < 0 || lpa == 0xffffffffffffffff)
			continue;
		if (partial == 1 && kpg_flags[loop] == KP_STT_DATA)
			continue;
		if (kpg_flags != NULL && (kpg_flags[loop] & KP_STT_DONE) == KP_STT_DONE)
			continue;
		ptr_data_org = (uint8_t*)__get_ramssd_data_addr (lpa);
		if ((pos = memcmp (ptr_page_data[loop], ptr_data_org, KPAGE_SIZE)) != 0) {
			if (pos < 0) pos *= -1;
			bdbm_msg ("[DATA CORRUPTION] lpa=%llu(%llx) offset=%llu (%p) pos=%d (HOST: %x %x %x %x %x != DRAM: %x %x %x %x %x)", 
				lpa, lpa,
				loop, 
				ptr_page_data[loop],
				pos,
				ptr_page_data[loop][pos-2], 
				ptr_page_data[loop][pos-1],
				ptr_page_data[loop][pos+0], 
				ptr_page_data[loop][pos+1],
				ptr_page_data[loop][pos+2],
				ptr_data_org[pos-2],
				ptr_data_org[pos-1], 
				ptr_data_org[pos+0], 
				ptr_data_org[pos+1],
				ptr_data_org[pos+2]);
		}
	}
#endif

fail:
	return ret;
}

static uint8_t __ramssd_prog_page (
	dev_ramssd_info_t* ri, 
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no,
	uint64_t page_no,
	kp_stt_t* kpg_flags,
	uint8_t** ptr_page_data,
	uint8_t* ptr_oob_data,
	uint8_t oob)
{
	uint8_t ret = 0;
	uint8_t* ptr_ramssd_addr = NULL;
	uint32_t nr_subpages, loop;

	/* get the memory address for the destined page */
	if ((ptr_ramssd_addr = __ramssd_page_addr (
			ri, channel_no, chip_no, block_no, page_no)) == NULL) {
		bdbm_error ("invalid ram addr (%p)", ptr_ramssd_addr);
		ret = 1;
		goto fail;
	}

	/* for better performance, RAMSSD directly copies the SSD data to pages */
	nr_subpages = ri->nand_params->page_main_size / KERNEL_PAGE_SIZE;
	if (ri->nand_params->page_main_size % KERNEL_PAGE_SIZE != 0) {
		bdbm_error ("The page-cache granularity (%lu) is not matched to the flash page size (%llu)", 
			KERNEL_PAGE_SIZE, ri->nand_params->page_main_size);
		ret = 1;
		goto fail;
	}

	/* copy the main page data to a buffer */
	for (loop = 0; loop < nr_subpages; loop++) {
		bdbm_memcpy (
			ptr_ramssd_addr + KERNEL_PAGE_SIZE * loop, 
			ptr_page_data[loop], 
			KERNEL_PAGE_SIZE
		);
	}

	/* copy the OOB data to a buffer */
	if (oob && ptr_oob_data != NULL) {
		bdbm_memcpy (
			ptr_ramssd_addr + ri->nand_params->page_main_size,
			ptr_oob_data,
			ri->nand_params->page_oob_size
		);
	}

#if defined (DATA_CHECK)
	/* TEMP */
	for (loop = 0; loop < nr_subpages; loop++) {
		uint64_t lpa = ((uint64_t*)ptr_oob_data)[loop];
		uint8_t* ptr_data_org = (uint8_t*)__get_ramssd_data_addr (lpa);
		memcpy (ptr_data_org, ptr_page_data[loop], KPAGE_SIZE);
	}
	/* TEMP */
#endif

fail:
	return ret;
}

static uint8_t __ramssd_erase_block (
	dev_ramssd_info_t* ri, 
	uint64_t channel_no,
	uint64_t chip_no,
	uint64_t block_no)
{
	uint8_t* ptr_ram_addr = NULL;

	/* get the memory address for the destined block */
	if ((ptr_ram_addr = __ramssd_block_addr 
			(ri, channel_no, chip_no, block_no)) == NULL) {
		bdbm_error ("invalid ssdram addr (%p)", ptr_ram_addr);
		return 1;
	}

	/* erase the block (set all the values to '1') */
	//memset (ptr_ram_addr, 0xFF, dev_ramssd_get_block_size (ri));

	return 0;
}

static uint32_t __ramssd_send_cmd (
	dev_ramssd_info_t* ri, bdbm_llm_req_t* ptr_req)
{
	uint8_t ret = 0;
	uint8_t use_oob = 1;	/* read or program OOB by default; why not??? */
	uint8_t use_partial = 0;

	if (ri->nand_params->page_oob_size == 0)
		use_oob = 0;

	switch (ptr_req->req_type) {
	case REQTYPE_RMW_READ:
		use_partial = 1;
	case REQTYPE_READ:
	case REQTYPE_META_READ:
	case REQTYPE_GC_READ:
		ret = __ramssd_read_page (
			ri, 
			ptr_req->phyaddr.channel_no, 
			ptr_req->phyaddr.chip_no, 
			ptr_req->phyaddr.block_no, 
			ptr_req->phyaddr.page_no, 
			ptr_req->fmain.kp_stt,
			ptr_req->fmain.kp_ptr,
			ptr_req->foob.data,
			use_oob,
			use_partial);
		break;

	case REQTYPE_RMW_WRITE:
#ifdef DBG_RMW
		bdbm_msg ("DEV-RMW_WRITE:  lpa=%llu (%llu %llu %llu %llu)", 
				ptr_req->logaddr.lpa[0],
			ptr_req->phyaddr.channel_no, 
			ptr_req->phyaddr.chip_no, 
			ptr_req->phyaddr.block_no, 
			ptr_req->phyaddr.page_no
				);
#endif
	case REQTYPE_WRITE:
	case REQTYPE_META_WRITE:
	case REQTYPE_GC_WRITE:
		ret = __ramssd_prog_page (
			ri, 
			ptr_req->phyaddr.channel_no,
			ptr_req->phyaddr.chip_no,
			ptr_req->phyaddr.block_no,
			ptr_req->phyaddr.page_no,
			ptr_req->fmain.kp_stt,
			ptr_req->fmain.kp_ptr,
			ptr_req->foob.data,
			use_oob);
		break;

	case REQTYPE_GC_ERASE:
		ret = __ramssd_erase_block (
			ri, 
			ptr_req->phyaddr.channel_no, 
			ptr_req->phyaddr.chip_no, 
			ptr_req->phyaddr.block_no);
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

	ptr_req->ret = ret;

	return ret;
}

void __ramssd_cmd_done (dev_ramssd_info_t* ri)
{
	uint64_t loop, nr_parallel_units;

	nr_parallel_units = dev_ramssd_get_chips_per_ssd (ri);

	for (loop = 0; loop < nr_parallel_units; loop++) {
		unsigned long flags;

		bdbm_spin_lock_irqsave (&ri->ramssd_lock, flags);
		if (ri->ptr_punits[loop].ptr_req != NULL) {
			dev_ramssd_punit_t* punit;
			int64_t elapsed_time_in_us;

			punit = &ri->ptr_punits[loop];
			elapsed_time_in_us = bdbm_stopwatch_get_elapsed_time_us (&punit->sw);

			if (elapsed_time_in_us >= punit->target_elapsed_time_us) {
				void* ptr_req = punit->ptr_req;
				punit->ptr_req = NULL;
				bdbm_spin_unlock_irqrestore (&ri->ramssd_lock, flags);

				/* call the interrupt handler */
				ri->intr_handler (ptr_req);
			} else {
				bdbm_spin_unlock_irqrestore (&ri->ramssd_lock, flags);
			}
		} else {
			bdbm_spin_unlock_irqrestore (&ri->ramssd_lock, flags);
		}
	}
}


/* Functions for Timing Management */
static void __ramssd_timing_cmd_done (unsigned long arg)
{
	/* forward it to ramssd_cmd_done */
	__ramssd_cmd_done ((dev_ramssd_info_t*)arg);
}

#if defined (KERNEL_MODE)
static enum hrtimer_restart __ramssd_timing_hrtimer_cmd_done (struct hrtimer *ptr_hrtimer)
{
	ktime_t ktime;
	dev_ramssd_info_t* ri;
	
	ri = (dev_ramssd_info_t*)container_of 
		(ptr_hrtimer, dev_ramssd_info_t, hrtimer);

	/* call a tasklet */
	tasklet_schedule (ri->tasklet); 

	ktime = ktime_set (0, 5 * 1000);
	hrtimer_start (&ri->hrtimer, ktime, HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}
#endif

uint32_t __ramssd_timing_register_schedule (dev_ramssd_info_t* ri)
{
	switch (ri->emul_mode) {
	case DEVICE_TYPE_RAMDRIVE:
	case DEVICE_TYPE_USER_RAMDRIVE:
		__ramssd_cmd_done (ri);
		break;
#if defined (KERNEL_MODE)
	case DEVICE_TYPE_RAMDRIVE_TIMING:
		/*__ramssd_cmd_done (ri);*/
		break;
	case DEVICE_TYPE_RAMDRIVE_INTR:
		tasklet_schedule (ri->tasklet); 
		break;
#endif
	default:
		__ramssd_timing_cmd_done ((unsigned long)ri);
		break;
	}

	return 0;
}

uint32_t __ramssd_timing_create (dev_ramssd_info_t* ri) 
{
	uint32_t ret = 0;

	switch (ri->emul_mode) {
	case DEVICE_TYPE_RAMDRIVE:
	case DEVICE_TYPE_USER_RAMDRIVE:
		break;
#if defined (KERNEL_MODE)
	case DEVICE_TYPE_RAMDRIVE_TIMING: 
		{
			ktime_t ktime;
			hrtimer_init (&ri->hrtimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
			ri->hrtimer.function = __ramssd_timing_hrtimer_cmd_done;
			ktime = ktime_set (0, 500 * 1000);
			hrtimer_start (&ri->hrtimer, ktime, HRTIMER_MODE_REL);
		}
		/* no break! we initialize a tasklet together */
	case DEVICE_TYPE_RAMDRIVE_INTR: 
		if ((ri->tasklet = (struct tasklet_struct*)
				bdbm_malloc_atomic (sizeof (struct tasklet_struct))) == NULL) {
			bdbm_error ("bdbm_malloc_atomic failed");
			ret = 1;
		} else {
			tasklet_init (ri->tasklet, 
				__ramssd_timing_cmd_done, (unsigned long)ri);
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

void __ramssd_timing_destory (dev_ramssd_info_t* ri)
{
	switch (ri->emul_mode) {
	case DEVICE_TYPE_RAMDRIVE:
	case DEVICE_TYPE_USER_RAMDRIVE:
		break;
#if defined (KERNEL_MODE)
	case DEVICE_TYPE_RAMDRIVE_TIMING:
		hrtimer_cancel (&ri->hrtimer);
		/* no break! we destroy a tasklet */
	case DEVICE_TYPE_RAMDRIVE_INTR:
		tasklet_kill (ri->tasklet);
		break;
#endif
	default:
		break;
	}
}

/* Functions Exposed to External Files */
dev_ramssd_info_t* dev_ramssd_create (
	bdbm_device_params_t* ptr_nand_params, 
	void (*intr_handler)(void*))
{
	uint64_t loop, nr_parallel_units;
	dev_ramssd_info_t* ri = NULL;

	/* create a ramssd info */
	if ((ri = (dev_ramssd_info_t*)
			bdbm_malloc_atomic (sizeof (dev_ramssd_info_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail;
	}

	/* seup parameters */
	ri->intr_handler = intr_handler;
	ri->emul_mode = ptr_nand_params->device_type;
	ri->nand_params = ptr_nand_params;

	/* allocate ssdram space */
	if ((ri->ptr_ssdram = 
			__ramssd_alloc_ssdram (ri->nand_params)) == NULL) {
		bdbm_error ("__ramssd_alloc_ssdram failed");
		goto fail_ssdram;
	}

	/* create parallel units */
	nr_parallel_units = dev_ramssd_get_chips_per_ssd (ri);

	if ((ri->ptr_punits = (dev_ramssd_punit_t*)
			bdbm_malloc_atomic (sizeof (dev_ramssd_punit_t) * nr_parallel_units)) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail_punits;
	}
	for (loop = 0; loop < nr_parallel_units; loop++) {
		ri->ptr_punits[loop].ptr_req = NULL;
	}

	/* create and register a tasklet */
	if (__ramssd_timing_create (ri) != 0) {
		bdbm_error ("__ramssd_timing_create () failed");
		goto fail_timing;
	}

	/* create spin_lock */
	bdbm_spin_lock_init (&ri->ramssd_lock);

	/* done */
	ri->is_init = 1;

	return ri;

fail_timing:
	bdbm_free_atomic (ri->ptr_punits);

fail_punits:
	__ramssd_free_ssdram (ri->ptr_ssdram);

fail_ssdram:
	bdbm_free_atomic (ri);

fail:
	return NULL;
}

void dev_ramssd_destroy (dev_ramssd_info_t* ri)
{
	/* kill tasklet */
	__ramssd_timing_destory (ri);

	/* free ssdram */
	__ramssd_free_ssdram (ri->ptr_ssdram);

	/* release other stuff */
	bdbm_free_atomic (ri->ptr_punits);
	bdbm_free_atomic (ri);
}

uint32_t dev_ramssd_send_cmd (dev_ramssd_info_t* ri, bdbm_llm_req_t* r)
{
	uint32_t ret;

	if ((ret = __ramssd_send_cmd (ri, r)) == 0) {
		unsigned long flags;
		int64_t target_elapsed_time_us = 0;
		uint64_t punit_id = r->phyaddr.punit_id;

		/* get the target elapsed time depending on the type of req */
		if (ri->emul_mode == DEVICE_TYPE_RAMDRIVE_TIMING) {
			switch (r->req_type) {
			case REQTYPE_WRITE:
			case REQTYPE_GC_WRITE:
			case REQTYPE_RMW_WRITE:
			case REQTYPE_META_WRITE:
				target_elapsed_time_us = ri->nand_params->page_prog_time_us;
				break;
			case REQTYPE_READ:
			case REQTYPE_GC_READ:
			case REQTYPE_RMW_READ:
			case REQTYPE_META_READ:
				target_elapsed_time_us = ri->nand_params->page_read_time_us;
				break;
			case REQTYPE_GC_ERASE:
				target_elapsed_time_us = ri->nand_params->block_erase_time_us;
				break;
			case REQTYPE_READ_DUMMY:
				target_elapsed_time_us = 0;	/* dummy read */
				break;
			default:
				bdbm_error ("invalid REQTYPE (%u)", r->req_type);
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
		bdbm_spin_lock_irqsave (&ri->ramssd_lock, flags);
		if (ri->ptr_punits[punit_id].ptr_req == NULL) {
			ri->ptr_punits[punit_id].ptr_req = (void*)r;
			/*ri->ptr_punits[punit_id].sw = bdbm_stopwatch_start ();*/
			bdbm_stopwatch_start (&ri->ptr_punits[punit_id].sw);
			ri->ptr_punits[punit_id].target_elapsed_time_us = target_elapsed_time_us;
		} else {
			bdbm_error ("More than two requests are assigned to the same parallel unit (ptr=%p, punit=%llu)",
				ri->ptr_punits[punit_id].ptr_req, punit_id);
			bdbm_spin_unlock_irqrestore (&ri->ramssd_lock, flags);
			ret = 1;
			goto fail;
		}
		bdbm_spin_unlock_irqrestore (&ri->ramssd_lock, flags);

		/* register reqs for callback */
		__ramssd_timing_register_schedule (ri);
	}

fail:
	return ret;
}

/* for snapshot */
uint32_t dev_ramssd_load (dev_ramssd_info_t* ri, const char* fn)
{
	/*struct file* fp = NULL;*/
	bdbm_file_t fp = 0;
	uint64_t len = 0;

	bdbm_msg ("dev_ramssd_load - begin");

	if (ri->ptr_ssdram == NULL) {
		bdbm_error ("ptr_ssdram is NULL");
		return 1;
	}
	
	if ((fp = bdbm_fopen (fn, O_RDWR, 0777)) == 0) {
		bdbm_error ("bdbm_fopen failed");
		return 1;
	}

	bdbm_msg ("dev_ramssd_load: DRAM read starts = %llu", len);
	len = dev_ramssd_get_ssd_size (ri);
	len = bdbm_fread (fp, 0, (uint8_t*)ri->ptr_ssdram, len);
	bdbm_msg ("dev_ramssd_load: DRAM read ends = %llu", len);

	bdbm_fclose (fp);

	bdbm_msg ("dev_ramssd_load - done");

	return 0;
}

uint32_t dev_ramssd_store (dev_ramssd_info_t* ri, const char* fn)
{
	/*struct file* fp = NULL;*/
	bdbm_file_t fp = 0;
	uint64_t pos = 0;
	uint64_t len = 0;

	bdbm_msg ("dev_ramssd_store - begin");

	if (ri->ptr_ssdram == NULL) {
		bdbm_error ("ptr_ssdram is NULL");
		return 1;
	}
	
	if ((fp = bdbm_fopen (fn, O_CREAT | O_WRONLY, 0777)) == 0) {
		bdbm_error ("bdbm_fopen failed");
		return 1;
	}

	len = dev_ramssd_get_ssd_size (ri);
	bdbm_msg ("dev_ramssd_store: DRAM store starts = %llu", len);
	while (pos < len) {
		pos += bdbm_fwrite (fp, pos, (uint8_t*)ri->ptr_ssdram + pos, len - pos);
	}
	bdbm_fsync (fp);
	bdbm_fclose (fp);

	bdbm_msg ("dev_ramssd_store: DRAM store ends = %llu", pos);
	bdbm_msg ("dev_ramssd_store - end");

	return 0;
}

