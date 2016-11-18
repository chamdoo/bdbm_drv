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

#ifdef	RFLUSH
#include <linux/bio.h>
#endif
#include "bdbm_drv.h"
#include "debug.h"
#include "params.h"
#include "umemory.h"
//#include "hlm_reqs_pool.h" // included in blkio.h
#include "blkio.h"
//#include "blkdev.h"
//#include "blkdev_ioctl.h"

#include "nvm_cache.h"

/* interface for nvm_dev */
bdbm_nvm_inf_t _nvm_dev = {
	.ptr_private = NULL,
	.create = bdbm_nvm_create,
	.destroy = bdbm_nvm_destroy,
	.make_req = bdbm_nvm_make_req,
//	.end_req = nvm_end_req,
};


static void __display_hex_values (uint8_t* host, uint8_t* back)
{
	bdbm_msg (" * HOST: %x %x %x %x %x != FLASH: %x %x %x %x %x", 
		host[0], host[1], host[2], host[3], host[4], 
		back[0], back[1], back[2], back[3], back[4]);
}
static void __display_hex_values_all (uint8_t* host, uint8_t* back)
{
	int i = 0;
	for (i = 0; i < KPAGE_SIZE; i+=4) {
		bdbm_msg (" * HOST: %x %x %x %x != FLASH: %x %x %x %x", 
			host[i+0], host[i+1], host[i+2], host[i+3],
			back[i+0], back[i+1], back[i+2], back[i+3]);
	}
}
static void __display_hex_values_all_host (uint8_t* host)
{
	int i = 0;
	for (i = 0; i < KPAGE_SIZE; i+=4) {
		bdbm_msg (" * HOST: %x %x %x %x", 
			host[i+0], host[i+1], host[i+2], host[i+3]);
	}
}
static void __display_hex_values_all_range (uint8_t* host, uint8_t* back, int size)
{
	int i = 0;
	for (i = 0; i < size; i+=4) {
		bdbm_msg (" * HOST: %x %x %x %x != FLASH: %x %x %x %x", 
			host[i+0], host[i+1], host[i+2], host[i+3],
			back[i+0], back[i+1], back[i+2], back[i+3]);
	}
}
static void __display_hex_values_all_host_range (uint8_t* host, int size)
{
	int i = 0;
	for (i = 0; i < size; i+=4) {
		bdbm_msg (" * HOST: %x %x %x %x", 
			host[i+0], host[i+1], host[i+2], host[i+3]);
	}
}


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
	bdbm_memset ((uint8_t*) ptr_nvmram, 0x00, nvm_size_in_bytes * sizeof (uint8_t));
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
		// index 
		me[i].index = i;

		// logaddr 
		me[i].logaddr.ofs = -1;
		for (j = 0; j < np->nr_subpages_per_page; j ++){
			me[i].logaddr.lpa[j] = -1;
		}
	}

	return me;
}

static void* __nvm_alloc_nvmram_lookup_tbl (bdbm_device_params_t* np) 
{
	bdbm_nvm_lookup_tbl_entry_t* me; 
	uint64_t i;

	/* allocate mapping entries */
	if ((me = (bdbm_nvm_lookup_tbl_entry_t*) bdbm_zmalloc 
		(sizeof (bdbm_nvm_lookup_tbl_entry_t) * np->nr_subpages_per_ssd)) == NULL) {
		return NULL;
	}

	/* initialize a mapping table */
	for (i = 0; i < np->nr_subpages_per_ssd; i++){
		// index 
		me[i].tbl_idx = -1;
#ifdef	RFLUSH
		me[i].ptr_page = NULL;
#endif
	}

	return me;
}




uint32_t bdbm_nvm_create (bdbm_drv_info_t* bdi){
	
	bdbm_nvm_dev_private_t* p = NULL;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	uint64_t i;

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
	p->nr_free_pages = p->nr_total_pages;
	p->nr_inuse_pages = 0;
	p->np = np;

	p->nr_total_access = 0;
	p->nr_total_write = 0;
	p->nr_total_read = 0;
	p->nr_write = 0;
	p->nr_nh_write = 0;
	p->nr_read = 0;	
	p->nr_nh_read = 0;	
	p->nr_total_hit = 0;
	p->nr_evict = 0;


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

	/* alloc lookup page tabl: ptr_nvm_lookup_tbl */
	if((p->ptr_nvm_lookup_tbl = __nvm_alloc_nvmram_lookup_tbl (np)) == NULL) {
		bdbm_error ("__alloc_nvmram table failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	/* initialize lock and list */
	bdbm_sema_init (&p->nvm_lock);

	if((p->lru_list = (struct list_head*) bdbm_zmalloc (sizeof(struct list_head))) == NULL) {
		bdbm_error ("__alloc nvmram lru_list failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}
	if((p->free_list = (struct list_head*) bdbm_zmalloc (sizeof(struct list_head))) == NULL) {
		bdbm_error ("__alloc nvmram free_list failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	INIT_LIST_HEAD(p->lru_list);
	INIT_LIST_HEAD(p->free_list);

	/* add all pages into free list */
	for (i=0; i<p->nr_total_pages; i++)
		list_add (&p->ptr_nvm_tbl[i].list, p->free_list);

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
		bdbm_free (p->ptr_nvm_tbl);
	}
	
	if(p->lru_list) {
		bdbm_free (p->lru_list);
	}

	if(p->free_list) {
		bdbm_free (p->free_list);
	}


	bdbm_free(p);

	return;
}

int64_t bdbm_nvm_find_data (bdbm_drv_info_t* bdi, bdbm_llm_req_t* lr)
{ 
	/* search nvm cache */
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_nvm_page_t* nvm_tbl = p->ptr_nvm_tbl;
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;
	uint64_t i = 0;
	int64_t found = -1;
	int64_t lpa;

	lpa = lr->logaddr.lpa[0];

	found = nvm_lookup_tbl[lpa].tbl_idx; 

#if 0
	for(i = 0; i < p->nr_total_pages; i++){
//		bdbm_msg("nvm_tbl[%llu] lpa = %d, tlpa = %d", i, nvm_tbl[i].logaddr.lpa[0], lpa);
		if(nvm_tbl[i].logaddr.lpa[0] == lpa){
//			bdbm_msg("hit: lpa = %llu", lpa);
			p->nr_total_hit++;
			atomic64_inc(&bdi->pm.nvm_h_cnt);
			found = i;	
			break;
		}
	}
#endif
	return found;
}

#ifdef	FLUSH
uint64_t bdbm_nvm_flush_data (bdbm_drv_info_t* bdi)
{ 
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;	// have lists lru_list and free_list
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;
	bdbm_hlm_req_t* hr = NULL;
	bdbm_nvm_page_t* fpage = NULL;
	uint8_t* fdata_ptr = NULL;
	int64_t findex = -1;
	int64_t flpa = -1;

	while (!list_empty(p->lru_list)) {
		bdbm_bug_on(list_empty(p->lru_list));
		fpage = list_last_entry(p->lru_list, bdbm_nvm_page_t, list);
		bdbm_bug_on(fpage == NULL);
		findex = fpage->index;
		fdata_ptr = p->ptr_nvmram + (findex * np->nvm_page_size);
		flpa = fpage->logaddr.lpa[0];

		/* get a free hlm_req from the hlm_reqs_pool */
		if ((hr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL) {
			bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
			goto fail;
		}
	
		/* build hlm_req with nvm_info */
		if (bdbm_hlm_reqs_pool_build_wb_req (hr, &fpage->logaddr, fdata_ptr) != 0) {
			bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
			goto fail;
		}

		/* send req */
		bdbm_sema_lock (&bp->host_lock);

		if(bdi->ptr_hlm_inf->make_wb_req (bdi, hr) != 0) {
			bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
		}

		bdbm_sema_unlock (&bp->host_lock);
	
		nvm_lookup_tbl[flpa].tbl_idx = -1;
#ifdef	RFLUSH
		nvm_lookup_tbl[flpa].ptr_page = NULL;
#endif
	
		list_del(&fpage->list);
		p->nr_inuse_pages--;
		list_add(&fpage->list, p->free_list);
		p->nr_free_pages++;
	}

	return 1; 

fail:
	if (hr)
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, hr);
	bdbm_bug_on(1);

	return -1;
}
#endif

uint64_t bdbm_nvm_read_data (bdbm_drv_info_t* bdi, bdbm_llm_req_t* lr)
{ 
	/* search nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	int64_t nvm_idx = -1;
	uint8_t* ptr_nvmram_addr = NULL; 

	p->nr_total_read++;
	atomic64_inc(&bdi->pm.nvm_r_cnt);

	/* search data */
	nvm_idx = bdbm_nvm_find_data(bdi, lr);

	if(nvm_idx < 0){
//		bdbm_msg("not found in nvm");
		return 0; // not found 
	}

	p->nr_read++;
	atomic64_inc(&bdi->pm.nvm_rh_cnt);

	/* get data addr */
	bdbm_bug_on(np->nr_subpages_per_page != 1);	
	bdbm_bug_on(np->page_main_size / KERNEL_PAGE_SIZE != 1);
	bdbm_bug_on(np->page_main_size != np->nvm_page_size);

	ptr_nvmram_addr = p->ptr_nvmram + (nvm_idx * np->nvm_page_size); 

	//bdbm_msg("nvm_idx =%d, ptr_nvm = %p, dst_addr= %p", 
	//	nvm_idx, ptr_nvmram_addr, lr->fmain.kp_ptr[0]);

	bdbm_bug_on(ptr_nvmram_addr == NULL);

	/* copy data */
	bdbm_bug_on(!lr);
	bdbm_bug_on(!lr->fmain.kp_ptr[0]);

	bdbm_memcpy(lr->fmain.kp_ptr[0], ptr_nvmram_addr, KERNEL_PAGE_SIZE);

	/* update lr req's status */
	lr->serviced_by_nvm = 1;
	return 1;
}

static int64_t bdbm_nvm_alloc_slot (bdbm_drv_info_t* bdi, bdbm_llm_req_t* lr)
{
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;

	bdbm_nvm_page_t* npage = NULL;
	bdbm_nvm_page_t* epage = NULL;
	uint8_t* edata_ptr = NULL;
	bdbm_hlm_req_t* hr = NULL;
	int64_t nindex = -1;
	int64_t eindex = -1;
	int64_t elpa = -1;
	int64_t lpa = -1;
#ifdef NVM_CACHE_DEBUG
	uint8_t* ptr_ramssd_data = NULL; 
#endif

	lpa = lr->logaddr.lpa[0];

	/* get a free page */
//	if(p->nr_free_pages > LOW_WATERMARK){
	if(p->nr_free_pages > 0){
//		bdbm_msg("get a free nvm buffer");
		bdbm_bug_on(list_empty(p->free_list));
		npage = list_last_entry(p->free_list, bdbm_nvm_page_t, list); 
		bdbm_bug_on(npage == NULL);
		nindex = npage->index;
		p->nr_free_pages --;
		p->nr_inuse_pages ++;
	}
	else { // eviction is needed 
#ifndef NVM_CACHE_WB
		return -1;
#endif
		p->nr_evict++;
		atomic64_inc(&bdi->pm.nvm_ev_cnt);

		bdbm_bug_on(!list_empty(p->free_list));
		epage = list_last_entry(p->lru_list, bdbm_nvm_page_t, list);
		bdbm_bug_on(epage == NULL);
		eindex = epage->index;
		edata_ptr = p->ptr_nvmram + (eindex * np->nvm_page_size); 
		elpa = epage->logaddr.lpa[0];

		bdbm_bug_on(!edata_ptr);
	
		/* get a free hlm_req from the hlm_reqs_pool */
		if((hr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL){
			bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
			goto fail;
		}

		/* build hlm_req with nvm_info */
		// 아래 함수에서 logaddr 을 많이 보내고, 그걸로 만들도록 하는게 좋을듯. 
		// 일단은 하나만 보내보자. 
		if (bdbm_hlm_reqs_pool_build_wb_req (hr, &epage->logaddr, edata_ptr) != 0) {
			bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
			goto fail;
		}
		/* hr->done is locked in pool_build_wb_req() */

		/* send req */
		bdbm_sema_lock (&bp->host_lock);

		if(bdi->ptr_hlm_inf->make_wb_req (bdi, hr) != 0) {
			bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
		}

#ifdef NVM_CACHE_DEBUG
#if 0
// 여기는 쓰기 전에 읽어와서 문제가 되는 듯. 의미 없을듯. 보내고 반영되기 전 or 다른 애가 변경한 후에 읽을 수 있음. 
 
		if(bdi->ptr_dm_inf->get_data){
			ptr_ramssd_data = bdi->ptr_dm_inf->get_data(bdi, epage->logaddr.lpa[0]);
		}
		bdbm_bug_on(edata_ptr == NULL);
		bdbm_bug_on(ptr_ramssd_data == NULL);

		if (memcmp(edata_ptr, ptr_ramssd_data, KPAGE_SIZE)!=0){
			bdbm_msg("[EUNJI] [DATA CORRUPTION] lpa = %d", epage->logaddr.lpa[0]);	
			__display_hex_values_all_range (edata_ptr, ptr_ramssd_data, 16);
			bdbm_bug_on(1);
		}
#endif
#endif

		bdbm_sema_unlock (&bp->host_lock);

		/* wait for write back to be completed */
		/* no need to wait. assume kp_ptr[0] gets allocated buffer in nvm */
//		bdbm_sema_lock (&hr->done);
// 		bdbm_sema_unlock (&hr->done);

//		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, hr); // moved to hlm_end_wb_req

		nvm_lookup_tbl[elpa].tbl_idx = -1;
#ifdef	RFLUSH
		nvm_lookup_tbl[elpa].ptr_page = NULL;
#endif

		/* set new page index */
		nindex = eindex;
		npage = epage;
	}

	bdbm_bug_on(nindex == -1);

	// add new node into lru list 

	// del links of prev and next  
	list_del(&npage->list);
	list_add(&npage->list, p->lru_list);
//	bdbm_msg("new nvm_buffer is added to lru_list");

	nvm_lookup_tbl[lpa].tbl_idx = nindex;
#ifdef	RFLUSH
	nvm_lookup_tbl[lpa].ptr_page = NULL;
#endif
	
	bdbm_bug_on(p->nr_free_pages + p->nr_inuse_pages != p->nr_total_pages);

	return nindex;

fail:
	if (hr) 
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, hr);
	bdbm_bug_on(1);
	
	return -1;

}

uint64_t bdbm_nvm_write_data (bdbm_drv_info_t* bdi, bdbm_llm_req_t* lr)
{ 
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_nvm_page_t* nvm_tbl = p->ptr_nvm_tbl;
	bdbm_hlm_req_t* hr = NULL;
	int64_t nvm_idx = -1;
	uint8_t* ptr_nvmram_addr = NULL;
	uint64_t i;

	p->nr_total_write++;
	atomic64_inc (&bdi->pm.nvm_w_cnt);

	/* find data */
	nvm_idx = bdbm_nvm_find_data(bdi, lr);

	/* write miss */
	if(nvm_idx < 0){
		p->nr_nh_write++;
//		bdbm_msg("alloc nvm for data write");
		if((nvm_idx = bdbm_nvm_alloc_slot(bdi, lr)) < 0)
			return 0;
	} else {	// hit
		p->nr_write++;
	}

//	p->nr_write++;
	atomic64_inc (&bdi->pm.nvm_wh_cnt);

	/* get data addr */
	bdbm_bug_on(np->nr_subpages_per_page != 1);	
	bdbm_bug_on(np->page_main_size / KERNEL_PAGE_SIZE != 1);
	bdbm_bug_on(np->page_main_size != np->nvm_page_size);

	ptr_nvmram_addr = p->ptr_nvmram + (nvm_idx * np->nvm_page_size); 
	bdbm_bug_on(!ptr_nvmram_addr);

	/* copy data */
	bdbm_memcpy(ptr_nvmram_addr, lr->fmain.kp_ptr[0], KERNEL_PAGE_SIZE);


	/* update logaddr */
	nvm_tbl[nvm_idx].logaddr.lpa[0] = lr->logaddr.lpa[0]; 

//	bdbm_msg("data write succeeds");
//	for(i = 0; i < p->nr_total_pages; i++){
//		bdbm_msg("nvm_tbl[%llu] lpa = %d", i, nvm_tbl[i].logaddr.lpa[0]);
//	}

	/* update lr req's status */
	lr->serviced_by_nvm = 1;

	/**********************************/
	/*								  */
	/* update page table or send trim */
	/*								  */
	/**********************************/

#ifdef NVM_CACHE_TRIM
//	bdbm_msg("[%s] send TRIM", __FUNCTION__);

	/* get a free hlm_req from the hlm_reqs_pool */
	if((hr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL){
		bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
		goto fail;
	}

	/* build trim hr with lpa, len */
	/* hr->done is locked in pool_build_wb_req() */
	if (bdbm_hlm_reqs_pool_build_int_trim_req (hr, lr->logaddr.lpa[0], 1) != 0) {
		bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
		goto fail;
	}


	/* send req */
	bdbm_sema_lock (&bp->host_lock);

	if(bdi->ptr_hlm_inf->make_req (bdi, hr) != 0) {
		bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
	}
	bdbm_sema_unlock (&bp->host_lock);
#endif
	return 1;

fail:
	if (hr) 
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, hr);
	return 1;	

}

#ifdef	RFLUSH
uint64_t bdbm_nvm_rflush_data (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr) {
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;	// have lists lru_list and free_list
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;
	bdbm_hlm_req_t* lhr = NULL;
	bdbm_nvm_page_t* fpage = NULL;
	uint8_t* fdata_ptr = NULL;
	int64_t findex = -1;
	sector_t i = 0;
	
	bdbm_blkio_req_t* br = (bdbm_blkio_req_t*) hr->blkio_req;
	struct bio* bi = (struct bio*) br->bio;
	sector_t lpamin = bi->bi_min;
	sector_t lpamax = bi->bi_max;

	for (i = lpamin; i <= lpamax; i++) {
		if (nvm_lookup_tbl[i].tbl_idx == -1)
			continue;
	
		fpage = nvm_lookup_tbl[i].ptr_page;
		bdbm_bug_on(fpage == NULL);
		findex = fpage->index;
		fdata_ptr = p->ptr_nvmram + (findex * np->nvm_page_size);
		/* get a free hlm_req from the hlm_reqs_pool */
        if ((lhr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL) {
			bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
            goto fail;
        }
				    
        /* build hlm_req with nvm_info */
        if (bdbm_hlm_reqs_pool_build_wb_req (lhr, &fpage->logaddr, fdata_ptr) != 0) {
            bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
            goto fail;
        }

        /* send req */
        bdbm_sema_lock (&bp->host_lock);

		if(bdi->ptr_hlm_inf->make_wb_req (bdi, lhr) != 0) {
            bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
        }

        bdbm_sema_unlock (&bp->host_lock);
        
		nvm_lookup_tbl[i].tbl_idx = -1; 
        nvm_lookup_tbl[i].ptr_page = NULL;
        
		list_del(&fpage->list);
        p->nr_inuse_pages--;
        list_add(&fpage->list, p->free_list);
        p->nr_free_pages++;
	}

	return 1;

fail:
	if (lhr)
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, lhr);
	bdbm_bug_on(1);

	return -1;
}
#endif

uint64_t bdbm_nvm_make_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr){
	/* find nvm cache */
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_llm_req_t* lr; 
	int64_t n, nr_remains;

	nr_remains = hr->nr_llm_reqs;

	bdbm_sema_lock (&p->nvm_lock);

#ifdef	FLUSH
	if (bdbm_is_flush (hr->req_type)) {
		bdbm_nvm_flush_data (bdi);
	}
#endif

	/* for nr_llm_reqs */	
	for (n = 0; n < hr->nr_llm_reqs; n++) {

		p->nr_total_access++;
		atomic64_inc(&bdi->pm.nvm_a_cnt);
	
		if ((p->nr_total_access % 100000) == 0){
//			bdbm_msg("nvm: total access = %llu, total read = %llu, read hit = %llu, total_write = %llu, write hit = %llu, hit = %llu, evict = %llu", 
//				p->nr_total_access, p->nr_total_read, p->nr_read, p->nr_total_write, p->nr_write, p->nr_total_hit, p->nr_evict);
			bdbm_msg("nvm: total access = %llu, total read = %llu, read hit = %llu, read no hit = %llu, total_write = %llu, write hit = %llu, write no hit = %llu, evict = %llu", 
				p->nr_total_access, p->nr_total_read, p->nr_read, p->nr_nh_read, p->nr_total_write, p->nr_write, p->nr_nh_write, p->nr_evict);
		}

		lr = &hr->llm_reqs[n];

		if (lr->req_type == REQTYPE_READ) {
			if(bdbm_nvm_read_data (bdi, &hr->llm_reqs[n]))
				nr_remains --; 	

		} else if (lr->req_type == REQTYPE_WRITE) {
			if(bdbm_nvm_write_data(bdi, &hr->llm_reqs[n]))
				nr_remains --;
		}
	}

#ifdef	RFLUSH
	if (bdbm_is_rflush (hr->req_type)) {
		bdbm_nvm_rflush_data (bdi, hr);
	}
#endif
	bdbm_sema_unlock (&p->nvm_lock);

	//bdbm_bug_on((hr->req_type == REQTYPE_WRITE) && (nr_remains != 0));

	return nr_remains;
}


#if 0
static int __hlm_reqs_pool_create_trim_req  (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* hr,
	bdbm_blkio_req_t* br)
{
	int64_t sec_start, sec_end;

	/* trim boundary sectors */
	sec_start = BDBM_ALIGN_UP (br->bi_offset, NR_KSECTORS_IN(pool->map_unit));
	sec_end = BDBM_ALIGN_DOWN (br->bi_offset + br->bi_size, NR_KSECTORS_IN(pool->map_unit));

	/* initialize variables */
	hr->req_type = br->bi_rw;
	bdbm_stopwatch_start (&hr->sw);
	if (sec_start < sec_end) {
		hr->lpa = (sec_start) / NR_KSECTORS_IN(pool->map_unit);
		hr->len = (sec_end - sec_start) / NR_KSECTORS_IN(pool->map_unit);
	} else {
		hr->lpa = (sec_start) / NR_KSECTORS_IN(pool->map_unit);
		hr->len = 0;
	}
	hr->blkio_req = (void*)br;
	hr->ret = 0;

	return 0;
}
#endif
