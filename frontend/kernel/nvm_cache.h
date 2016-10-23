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

//#ifndef _BLUEDBM_HOST_BLKDEV_H
//#define _BLUEDBM_HOST_BLKDEV_H

extern bdbm_nvm_inf_t _nvm_dev;


typedef struct {
	uint8_t status;
	bdbm_logaddr_t logaddr;
//	bdbm_phyaddr_t phyaddr;
//	struct list_head list;	/* for lru list */
	void* ptr_nvmram_data;
} bdbm_nvm_block_t;

typedef struct {
	bdbm_device_params_t* np;
	uint64_t nr_total_blks;
	bdbm_spinlock_t nvm_lock;

	bdbm_nvm_block_t* ptr_nvm_tbl;

	void* ptr_nvmram; /* DRAM memory for nvm */
	struct list_head lru_list;

//	bdbm_nvm_block_t* ptr_lru_list;

} bdbm_nvm_dev_private_t;

uint32_t bdbm_nvm_create (bdbm_drv_info_t* bdi);
//#endif



