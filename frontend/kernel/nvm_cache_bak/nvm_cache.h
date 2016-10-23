#if defined (KERNEL_MODE)
#include <linux/module.h>
//#include <linux/slab.h>
//include <linux/list.h>
//#include <linux/types.h>

#elif defined (USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

//#include "bdbm_drv.h"
//#include "debug.h"
//#include "umemory.h"
//#include "params.h"
//#include "utime.h"
//#include "uthread.h"


enum BDBM_NVM_BLK_STATUS {
	BDBM_NVM_BLK_FREE = 0,
	BDBM_NVM_BLK_CLEAN,
	BDBM_NVM_BLK_DIRTY,
};

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


#if 0
bdbm_abm_info_t* bdbm_abm_create (bdbm_device_params_t* np, uint8_t use_pst);
void bdbm_abm_destroy (bdbm_abm_info_t* bai);
bdbm_abm_block_t* bdbm_abm_get_block (bdbm_abm_info_t* bai, uint64_t channel_no, uint64_t chip_no, uint64_t block_no);
bdbm_abm_block_t* bdbm_abm_get_free_block_prepare (bdbm_abm_info_t* bai, uint64_t channel_no, uint64_t chip_no);
void bdbm_abm_get_free_block_rollback (bdbm_abm_info_t* bai, bdbm_abm_block_t* blk);
void bdbm_abm_get_free_block_commit (bdbm_abm_info_t* bai, bdbm_abm_block_t* blk);
void bdbm_abm_erase_block (bdbm_abm_info_t* bai, uint64_t channel_no, uint64_t chip_no, uint64_t block_no, uint8_t is_bad);
void bdbm_abm_invalidate_page (bdbm_abm_info_t* bai, uint64_t channel_no, uint64_t chip_no, uint64_t block_no, uint64_t page_no, uint64_t subpage_no);
void bdbm_abm_set_to_dirty_block (bdbm_abm_info_t* bai, uint64_t channel_no, uint64_t chip_no, uint64_t block_no);

static inline uint64_t bdbm_abm_get_nr_free_blocks (bdbm_abm_info_t* bai) { return bai->nr_free_blks; }
static inline uint64_t bdbm_abm_get_nr_free_blocks_prepared (bdbm_abm_info_t* bai) { return bai->nr_free_blks_prepared; }
static inline uint64_t bdbm_abm_get_nr_clean_blocks (bdbm_abm_info_t* bai) { return bai->nr_clean_blks; }
static inline uint64_t bdbm_abm_get_nr_dirty_blocks (bdbm_abm_info_t* bai) { return bai->nr_dirty_blks; }
static inline uint64_t bdbm_abm_get_nr_total_blocks (bdbm_abm_info_t* bai) { return bai->nr_total_blks; }

uint32_t bdbm_abm_load (bdbm_abm_info_t* bai, const char* fn);
uint32_t bdbm_abm_store (bdbm_abm_info_t* bai, const char* fn);

#define bdbm_abm_list_for_each_dirty_block(pos, bai, channel_no, chip_no) \
	list_for_each (pos, &(bai->list_head_dirty[channel_no][chip_no]))
#define bdbm_abm_fetch_dirty_block(pos) \
	list_entry (pos, bdbm_abm_block_t, list)
/*  (example:)
 *  bdbm_abm_list_for_each_dirty_block (pos, p->bai, j, k) {
		b = bdbm_abm_fetch_dirty_block (pos);
	}
 */

#endif
