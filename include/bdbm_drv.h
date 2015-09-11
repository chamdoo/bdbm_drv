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

#ifndef _BLUEDBM_DRV_H
#define _BLUEDBM_DRV_H

#if defined(KERNEL_MODE)
#define KERNEL_PAGE_SIZE	PAGE_SIZE

#elif defined(USER_MODE)
#include <stdint.h>
#include "3rd/uatomic.h"
#include "3rd/uatomic64.h"
#include "3rd/ulist.h"

#define KERNEL_PAGE_SIZE	4096	/* a default page size */

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "params.h"
#include "utils/utime.h"
#include "platform.h"

/* useful macros */
#define BDBM_KB (1024)
#define BDBM_MB (1024 * 1024)
#define BDBM_GB (1024 * 1024 * 1024)
#define BDBM_TB (1024 * 1024 * 1024 * 1024)

#define BDBM_SIZE_KB(size) (size/BDBM_KB)
#define BDBM_SIZE_MB(size) (size/BDBM_MB)
#define BDBM_SIZE_GB(size) (size/BDBM_GB)

typedef struct _bdbm_drv_info_t bdbm_drv_info_t;

/* for performance monitoring */
typedef struct {
	bdbm_spinlock_t pmu_lock;
	bdbm_stopwatch_t exetime;
	atomic64_t page_read_cnt;
	atomic64_t page_write_cnt;
	atomic64_t rmw_read_cnt;
	atomic64_t rmw_write_cnt;
	atomic64_t gc_cnt;
	atomic64_t gc_erase_cnt;
	atomic64_t gc_read_cnt;
	atomic64_t gc_write_cnt;
	atomic64_t meta_read_cnt;
	atomic64_t meta_write_cnt;
	uint64_t time_r_sw;
	uint64_t time_r_q;
	uint64_t time_r_tot;
	uint64_t time_w_sw;
	uint64_t time_w_q;
	uint64_t time_w_tot;
	uint64_t time_rmw_sw;
	uint64_t time_rmw_q;
	uint64_t time_rmw_tot;
	uint64_t time_gc_sw;
	uint64_t time_gc_q;
	uint64_t time_gc_tot;
	atomic64_t* util_r;
	atomic64_t* util_w;
} bdbm_perf_monitor_t;

#define BDBM_GET_HOST_INF(bdi) bdi->ptr_host_inf
#define BDBM_GET_DM_INF(bdi) bdi->ptr_dm_inf
#define BDBM_GET_HLM_INF(bdi) bdi->ptr_hlm_inf
#define BDBM_GET_LLM_INF(bdi) bdi->ptr_llm_inf
#define BDBM_GET_NAND_PARAMS(bdi) (&bdi->ptr_bdbm_params->nand)
#define BDBM_GET_DRIVER_PARAMS(bdi) (&bdi->ptr_bdbm_params->driver)
#define BDBM_GET_FTL_INF(bdi) bdi->ptr_ftl_inf

#define BDBM_HOST_PRIV(bdi) bdi->ptr_host_inf->ptr_private
#define BDBM_DM_PRIV(bdi) bdi->ptr_dm_inf->ptr_private
#define BDBM_HLM_PRIV(bdi) bdi->ptr_hlm_inf->ptr_private
#define BDBM_LLM_PRIV(bdi) bdi->ptr_llm_inf->ptr_private
#define BDBM_FTL_PRIV(bdi) bdi->ptr_ftl_inf->ptr_private

#define GET_PUNIT_ID(bdi,phyaddr) \
	phyaddr->channel_no * \
	bdi->ptr_bdbm_params->nand.nr_chips_per_channel + \
	phyaddr->chip_no

/* request types */
enum BDBM_REQTYPE {
	/* reqtype from host */
	REQTYPE_READ = 0,
	REQTYPE_READ_DUMMY = 1,
	REQTYPE_WRITE = 2,
	REQTYPE_RMW_READ = 3, 		/* Read-Modify-Write */
	REQTYPE_RMW_WRITE = 4, 		/* Read-Modify-Write */
	REQTYPE_GC_READ = 5,
	REQTYPE_GC_WRITE = 6,
	REQTYPE_GC_ERASE = 7,
	REQTYPE_TRIM = 8,

	REQTYPE_META_READ = 9,
	REQTYPE_META_WRITE = 10,
};

/* a physical address */
typedef struct {
	uint64_t punit_id;
	uint64_t channel_no;
	uint64_t chip_no;
	uint64_t block_no;
	uint64_t page_no;
} bdbm_phyaddr_t;

/* a high-level memory manager request */
enum BDBM_HLM_MEMFLAG {
	MEMFLAG_NOT_SET = 0,
	MEMFLAG_FRAG_PAGE = 1,
	MEMFLAG_KMAP_PAGE = 2,
	MEMFLAG_MAPBLK_PAGE = 3,
	MEMFLAG_DONE = 0x80,
	MEMFLAG_FRAG_PAGE_DONE = MEMFLAG_FRAG_PAGE | MEMFLAG_DONE,
	MEMFLAG_KMAP_PAGE_DONE = MEMFLAG_KMAP_PAGE | MEMFLAG_DONE,
	MEMFLAG_MAPBLK_PAGE_DONE = MEMFLAG_MAPBLK_PAGE | MEMFLAG_DONE,
};

/* a high-level request */
typedef struct {
	uint64_t uniq_id; /* for debugging */
	uint32_t req_type; /* read, write, or trim */
	uint64_t lpa; /* logical page address */
	uint64_t len; /* legnth */
	uint64_t nr_done_reqs;	/* # of llm_reqs served */
	uint8_t* kpg_flags;
	uint8_t** pptr_kpgs; /* data for individual kernel pages */
	void* ptr_host_req; /* struct bio or I/O trace */
	uint8_t ret;
	uint8_t queued;
	bdbm_spinlock_t lock; /* spinlock */

	/* for performance monitoring */
	bdbm_stopwatch_t sw;

	/* temp */
	uint8_t* org_kpg_flags;
	uint8_t** org_pptr_kpgs; /* data for individual kernel pages */
	/* end */

	bdbm_mutex_t* done;
} bdbm_hlm_req_t;

/* a low-level request */
typedef struct {
	uint32_t req_type; /* read, write, or erase */
	uint64_t lpa; /* logical page address */
	bdbm_phyaddr_t* phyaddr;	/* current */
	bdbm_phyaddr_t phyaddr_r; /* for reads */
	bdbm_phyaddr_t phyaddr_w; /* for writes */
	uint8_t* kpg_flags;
	uint8_t** pptr_kpgs; /* from bdbm_hlm_req_t */
	uint8_t* ptr_oob;
	void* ptr_hlm_req;
	/*struct list_head list;	*//* for list management */
	void* ptr_qitem;
	uint8_t ret;	/* old for GC */

	/* for dftl */
	/*bdbm_mutex_t* done;*/
	bdbm_completion_t* done; 
	void* ds;
} bdbm_llm_req_t;

/* a high-level request for gc */
typedef struct {
	uint32_t req_type;
	uint64_t nr_done_reqs;
	uint64_t nr_reqs;
	bdbm_llm_req_t* llm_reqs;
	bdbm_mutex_t gc_done;
} bdbm_hlm_req_gc_t;

/* a generic host interface */
typedef struct {
	void* ptr_private;
	uint32_t (*open) (bdbm_drv_info_t* bdi);
	void (*close) (bdbm_drv_info_t* bdi);
	void (*make_req) (bdbm_drv_info_t* bdi, void* req);
	void (*end_req) (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* req);
} bdbm_host_inf_t;

/* a generic high-level memory manager interface */
typedef struct {
	void* ptr_private;
	uint32_t (*create) (bdbm_drv_info_t* bdi);
	void (*destroy) (bdbm_drv_info_t* bdi);
	uint32_t (*make_req) (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* req);
	void (*end_req) (bdbm_drv_info_t* bdi, bdbm_llm_req_t* req);
} bdbm_hlm_inf_t;

/* a generic low-level memory manager interface */
typedef struct {
	void* ptr_private;
	uint32_t (*create) (bdbm_drv_info_t* bdi);
	void (*destroy) (bdbm_drv_info_t* bdi);
	uint32_t (*make_req) (bdbm_drv_info_t* bdi, bdbm_llm_req_t* req);
	void (*flush) (bdbm_drv_info_t* bdi);
	void (*end_req) (bdbm_drv_info_t* bdi, bdbm_llm_req_t* req);
} bdbm_llm_inf_t;

/* a generic device interface */
typedef struct {
	void* ptr_private;
	uint32_t (*probe) (bdbm_drv_info_t* bdi, nand_params_t* param);
	uint32_t (*open) (bdbm_drv_info_t* bdi);
	void (*close) (bdbm_drv_info_t* bdi);
	uint32_t (*make_req) (bdbm_drv_info_t* bdi, bdbm_llm_req_t* req);
	void (*end_req) (bdbm_drv_info_t* bdi, bdbm_llm_req_t* req);
	uint32_t (*load) (bdbm_drv_info_t* bdi, const char* fn);
	uint32_t (*store) (bdbm_drv_info_t* bdi, const char* fn);
} bdbm_dm_inf_t;

/* a generic queue interface */
typedef struct {
	void* ptr_private;
	uint32_t (*create) (bdbm_drv_info_t* bdi, uint64_t nr_punits, uint64_t nr_items_per_pu);
	void (*destroy) (bdbm_drv_info_t* bdi);
	uint32_t (*enqueue) (bdbm_drv_info_t* bdi, uint64_t punit, void* req);
	void* (*dequeue) (bdbm_drv_info_t* bdi, uint64_t punit);
	uint8_t (*is_full) (bdbm_drv_info_t* bdi, uint64_t punit);
	uint8_t (*is_empty) (bdbm_drv_info_t* bdi, uint64_t punit);
} bdbm_queue_inf_t;

/* a generic FTL interface */
typedef struct {
	void* ptr_private;
	uint32_t (*create) (bdbm_drv_info_t* bdi);
	void (*destroy) (bdbm_drv_info_t* bdi);
	uint32_t (*get_free_ppa) (bdbm_drv_info_t* bdi, uint64_t lpa, bdbm_phyaddr_t* ppa);
	uint32_t (*get_ppa) (bdbm_drv_info_t* bdi, uint64_t lpa, bdbm_phyaddr_t* ppa);
	uint32_t (*map_lpa_to_ppa) (bdbm_drv_info_t* bdi, uint64_t lpa, bdbm_phyaddr_t* ppa);
	uint32_t (*invalidate_lpa) (bdbm_drv_info_t* bdi, uint64_t lpa, uint64_t len);
	uint32_t (*do_gc) (bdbm_drv_info_t* bdi);
	uint8_t (*is_gc_needed) (bdbm_drv_info_t* bdi);

	/* interfaces for intialization */
	uint32_t (*scan_badblocks) (bdbm_drv_info_t* bdi);
	uint32_t (*load) (bdbm_drv_info_t* bdi, const char* fn);
	uint32_t (*store) (bdbm_drv_info_t* bdi, const char* fn);
	
	/* interfaces for RSD */
	uint64_t (*get_segno) (bdbm_drv_info_t* bdi, uint64_t lpa);

	/* interfaces for DFTL */
	uint8_t (*check_mapblk) (bdbm_drv_info_t* bdi, uint64_t lpa);
	bdbm_llm_req_t* (*prepare_mapblk_eviction) (bdbm_drv_info_t* bdi);
	void (*finish_mapblk_eviction) (bdbm_drv_info_t* bdi, bdbm_llm_req_t* r);
	bdbm_llm_req_t* (*prepare_mapblk_load) (bdbm_drv_info_t* bdi, uint64_t lpa);
	void (*finish_mapblk_load) (bdbm_drv_info_t* bdi, bdbm_llm_req_t* r);
} bdbm_ftl_inf_t;

/* the main data-structure for bdbm_drv */
struct _bdbm_drv_info_t {
	void* private_data;
	bdbm_params_t* ptr_bdbm_params;
	bdbm_host_inf_t* ptr_host_inf; 
	bdbm_dm_inf_t* ptr_dm_inf;
	bdbm_hlm_inf_t* ptr_hlm_inf;
	bdbm_llm_inf_t* ptr_llm_inf;
	bdbm_ftl_inf_t* ptr_ftl_inf;
	bdbm_perf_monitor_t pm;
};

#endif /* _BLUEDBM_DRV_H */
