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

#ifndef _BLUEDBM_PARAMS_H
#define _BLUEDBM_PARAMS_H


#define KERNEL_SECTOR_SIZE				512		/* kernel sector size is usually set to 512 bytes */


/* default NAND parameters (just for convenience purpose) */
enum DEFAULT_NAND_PARAMS {
	NAND_PAGE_SIZE = 4096,
	NAND_PAGE_OOB_SIZE = 64,
	NR_PAGES_PER_BLOCK = 128,
	NR_BLOCKS_PER_CHIP = 32*4,
	NR_CHIPS_PER_CHANNEL = 4,
	NR_CHANNELS = 8,
	NAND_HOST_BUS_TRANS_TIME_US = 0,	/* assume to be 0 */
	NAND_CHIP_BUS_TRANS_TIME_US = 100,	/* 100us */
	NAND_PAGE_PROG_TIME_US = 500,		/* 1.3ms */	
	NAND_PAGE_READ_TIME_US = 100,		/* 100us */
	NAND_BLOCK_ERASE_TIME_US = 3000,	/* 3ms */
	BLKOFS = 256,
};

/* device-type parameters */
enum DEVICE_TYPE {
	DEVICE_TYPE_RAMDRIVE = 1,
	DEVICE_TYPE_RAMDRIVE_INTR,
	DEVICE_TYPE_RAMDRIVE_TIMING, 
	DEVICE_TYPE_BLUEDBM,
	DEVICE_TYPE_USER_DUMMY,
	DEVICE_TYPE_USER_RAMDRIVE,
	DEVICE_TYPE_NOTSET = 0xFF,
};

/* default parameters for a device driver */
enum MAPPING_POLICY {
	MAPPING_POLICY_NO_FTL = 1,
	MAPPING_POLICY_SEGMENT = 2,
	MAPPING_POLICY_PAGE = 3,
	MAPPING_POLICY_DFTL = 4,
};

enum GC_POLICY {
	GC_POLICY_MERGE = 1,
	GC_POLICY_RAMDOM = 2,
	GC_POLICY_GREEDY = 3,
	GC_POLICY_COST_BENEFIT = 4,
};

enum WL_POLICY {
	WL_POLICY_NONE = 1,
	WL_DUAL_POOL = 2,
};

enum QUEUE_POLICY {
	QUEUE_NO = 1,
	QUEUE_SINGLE_FIFO = 2,
	QUEUE_MULTI_FIFO = 3,
};

enum TRIM {
	TRIM_ENABLE = 1, /* 1: enable, 2: disable */
	TRIM_DISABLE = 2,
};

enum HOST_TYPE {
	HOST_BLOCK = 1,
	HOST_DIRECT = 2,
};

enum LLM_TYPE {
	LLM_NO_QUEUE = 1,
	LLM_MULTI_QUEUE = 2,
};

enum HLM_TYPE {
	HLM_NO_BUFFER = 1,
	HLM_BUFFER,
	HLM_DFTL,
	HLM_RSD,
};


/* parameter structures */
typedef struct {
	uint32_t mapping_policy;
	uint32_t gc_policy;
	uint32_t wl_policy;
	uint32_t kernel_sector_size;
	uint32_t queueing_policy;
	uint32_t trim;
	uint32_t host_type;
	uint32_t llm_type;
	uint32_t hlm_type;
	uint32_t mapping_type;
} driver_params_t;

typedef struct {
	uint64_t nr_channels;
	uint64_t nr_chips_per_channel;
	uint64_t nr_blocks_per_chip;
	uint64_t nr_pages_per_block;
	uint64_t page_main_size;
	uint64_t page_oob_size;
	uint32_t device_type;
	uint64_t device_capacity_in_byte;
	uint64_t page_prog_time_us;
	uint64_t page_read_time_us;
	uint64_t block_erase_time_us;

	uint64_t nr_blocks_per_channel;
	uint64_t nr_blocks_per_ssd;
	uint64_t nr_chips_per_ssd;
	uint64_t nr_pages_per_ssd;
} nand_params_t;

typedef struct {
	driver_params_t driver;
	nand_params_t nand;
} bdbm_params_t;

#endif /* _BLUEDBM_PARAMS_H */
