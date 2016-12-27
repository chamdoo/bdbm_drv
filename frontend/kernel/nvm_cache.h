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


#define NVM_BLK_SIZE 4096

extern bdbm_nvm_inf_t _nvm_dev;


typedef struct {
	uint8_t status;
	int64_t index;
	bdbm_logaddr_t logaddr;
//	bdbm_phyaddr_t phyaddr;
	struct list_head list;	/* for lru list */
} bdbm_nvm_page_t;

typedef struct {
	int64_t tbl_idx;
#ifdef	RFLUSH
	bdbm_nvm_page_t* ptr_page;
#endif
} bdbm_nvm_lookup_tbl_entry_t;

typedef struct {
	bdbm_device_params_t* np;
	uint64_t nr_total_pages;
	uint64_t nr_free_pages;
	uint64_t nr_inuse_pages; 

	uint64_t nr_total_access;
	uint64_t nr_total_write;
	uint64_t nr_total_read;
	uint64_t nr_write; //count of hit
	uint64_t nr_nh_write; //count of no hit
	uint64_t nr_read;
	uint64_t nr_nh_read;
	uint64_t nr_total_hit;
	uint64_t nr_evict;

	void* ptr_nvmram; /* DRAM memory for nvm */
//	bdbm_nvm_page_t* ptr_nvm_rb_tree;
	bdbm_nvm_page_t* ptr_nvm_tbl;
	//bdbm_nvm_page_t* ptr_nvm_lookup_tbl;
	bdbm_nvm_lookup_tbl_entry_t* ptr_nvm_lookup_tbl;

	bdbm_sema_t nvm_lock;
	struct list_head* lru_list;
	struct list_head* free_list;

//	bdbm_nvm_block_t* ptr_lru_list;

} bdbm_nvm_dev_private_t;

uint32_t bdbm_nvm_create (bdbm_drv_info_t* bdi);
void bdbm_nvm_destroy (bdbm_drv_info_t* bdi);
uint64_t bdbm_nvm_make_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr);
uint64_t bdbm_nvm_rflush_data (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr);
uint64_t bdbm_nvm_flush_data (bdbm_drv_info_t* bdi);

//#endif



