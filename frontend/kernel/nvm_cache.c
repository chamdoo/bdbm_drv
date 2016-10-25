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

#include <linux/module.h> /* uint64_t */
//#include <linux/blkdev.h> /* bio */
//#include <linux/hdreg.h>
//#include <linux/kthread.h>
//#include <linux/delay.h> /* mdelay */

#include "bdbm_drv.h"
#include "debug.h"
#include "params.h"
#include "umemory.h"
//#include "blkdev.h"
//#include "blkdev_ioctl.h"

#ifdef NVM_CACHE

#include "nvm_cache.h"

/* interface for nvm_dev */
bdbm_nvm_inf_t _nvm_dev = {
	.ptr_private = NULL,
	.create = bdbm_nvm_create,
	.destroy = bdbm_nvm_destroy,
//	.make_req = nvm_make_req,
//	.end_req = nvm_end_req,
};


static void* __nvm_alloc_nvmram (bdbm_device_params_t* ptr_np) 
{
	void* ptr_nvmram = NULL;
	
	uint64_t page_size_in_bytes = ptr_np->nvm_page_size; 
	uint64_t nvm_size_in_bytes;

	nvm_size_in_bytes = 
		page_size_in_bytes * ptr_np->nr_nvm_pages;

	if((ptr_nvmram = (void*) bdbm_malloc
		(nvm_size_in_bytes * sizeof(uint8_t))) == NULL) {
		bdbm_error("bdbm_malloc failed (nvm size = %llu bytes)", nvm_size_in_bytes);
		return NULL;
	}
	bdbm_memset ((uint8_t*) ptr_nvmram, 0xFF, nvm_size_in_bytes * sizeof (uint8_t));
	bdbm_msg("nvm cache addr = %p", ptr_nvmram);

	return (void*) ptr_nvmram;

}


static void* __nvm_alloc_nvmram_tbl (bdbm_device_params_t* np) 
{
	bdbm_nvm_page_t* me; 
	uint64_t i, j;

	/* allocate mapping entries */
	if ((me = (bdbm_nvm_page_t*) bdbm_zmalloc 
		(sizeof (bdbm_nvm_page_t) * np->nr_nvm_pages)) == NULL) {
		return NULL;
	}

	/* initialize a mapping table */
	for (i = 0; i < np->nr_nvm_pages; i++){
		me[i].logaddr.ofs = -1;
		for (j = 0; j < np->nr_subpages_per_page; j ++){
			me[i].logaddr.lpa[j] = -1;
		}
	}

	return me;
}




uint32_t bdbm_nvm_create (bdbm_drv_info_t* bdi){
	
	bdbm_nvm_dev_private_t* p = NULL;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);

	if (!bdi->ptr_nvm_inf)
		return 1;

	/* create a private data structure */
	if ((p = (bdbm_nvm_dev_private_t*)bdbm_zmalloc 
			(sizeof (bdbm_nvm_dev_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	/* assign p into ptr_private to destroy it properly upon a fail */
	_nvm_dev.ptr_private = (void*)p;

	p->nr_total_pages = np->nr_nvm_pages;
	p->np = np;

	/* alloc ptr_nvmram_data: ptr_nvm_data */
	if((p->ptr_nvmram = __nvm_alloc_nvmram (np)) == NULL) {
		bdbm_error ("__alloc_nvmram failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	/* alloc page table: ptr_nvm_tbl */	
	if((p->ptr_nvm_tbl = __nvm_alloc_nvmram_tbl (np)) == NULL) {
		bdbm_error ("__alloc_nvmram table failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	/* initialize lock and list */
	bdbm_spin_lock_init (&p->nvm_lock);

	if((p->lru_list = (struct list_head*) bdbm_zmalloc (sizeof(struct list_head))) == NULL) {
		bdbm_error ("__alloc nvmram lru_list failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	bdbm_msg("==========================================================");
	bdbm_msg("NVM CONFIGURATION");
	bdbm_msg("==========================================================");
	bdbm_msg("total size = %llu, nr_nvm_pages = %llu, nvm_page_size	= %llu",
		np->nr_nvm_pages * np->nvm_page_size, np->nr_nvm_pages, np->nvm_page_size);

	return 0;
}


void bdbm_nvm_destroy (bdbm_drv_info_t* bdi)
{
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;  

	if(!p)
		return;

	if(p->ptr_nvmram) {
		bdbm_free (p->ptr_nvmram);
	}

	if(p->ptr_nvm_tbl) {
		bdbm_free (p->ptr_nvmram);
	}
	
	if(p->lru_list) {
		bdbm_free (p->lru_list);
	}

	bdbm_free(p);

	return;
}

#endif
