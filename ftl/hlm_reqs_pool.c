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

#if defined(KERNEL_MODE)
#include <linux/module.h>
#include <linux/blkdev.h>

#elif defined(USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "debug.h"
#include "params.h"
#include "bdbm_drv.h"
#include "hlm_reqs_pool.h"


#define DEFAULT_POOL_SIZE		128
#define DEFAULT_POOL_INC_SIZE	DEFAULT_POOL_SIZE / 5

bdbm_hlm_reqs_pool_t* bdbm_hlm_reqs_pool_create (
	int32_t mapping_unit_size, 
	int32_t io_unit_size)
{
	bdbm_hlm_reqs_pool_t* pool = NULL;
	int i = 0;

	/* create a pool structure */
	if ((pool = bdbm_malloc (sizeof (bdbm_hlm_reqs_pool_t))) == NULL) {
		bdbm_error ("bdbm_malloc () failed");
		return NULL;
	}

	/* initialize variables */
	bdbm_spin_lock_init (&pool->lock);
	INIT_LIST_HEAD (&pool->used_list);
	INIT_LIST_HEAD (&pool->free_list);
	pool->pool_size = DEFAULT_POOL_SIZE;
	pool->map_unit = mapping_unit_size;
	pool->io_unit = io_unit_size;

	/* add hlm_reqs to the free-list */
	for (i = 0; i < DEFAULT_POOL_SIZE; i++) {
		bdbm_hlm_req_t* item = NULL;
		if ((item = (bdbm_hlm_req_t*)bdbm_malloc (sizeof (bdbm_hlm_req_t))) == NULL) {
			bdbm_error ("bdbm_malloc () failed");
			goto fail;
		}
		list_add_tail (&item->list, &pool->free_list);
		bdbm_mutex_init (&item->done);
	}

	return pool;

fail:
	/* oops! it failed */
	if (pool) {
		struct list_head* next = NULL;
		struct list_head* temp = NULL;
		bdbm_hlm_req_t* item = NULL;
		list_for_each_safe (next, temp, &pool->free_list) {
			item = list_entry (next, bdbm_hlm_req_t, list);
			list_del (&item->list);
			bdbm_free (item);
		}
		bdbm_spin_lock_destory (&pool->lock);
		bdbm_free (pool);
		pool = NULL;
	}
	return NULL;
}

void bdbm_hlm_reqs_pool_destroy (
	bdbm_hlm_reqs_pool_t* pool)
{
	struct list_head* next = NULL;
	struct list_head* temp = NULL;
	bdbm_hlm_req_t* item = NULL;
	uint64_t count = 0;

	if (!pool) return;

	/* free & remove items from the used_list */
	list_for_each_safe (next, temp, &pool->used_list) {
		item = list_entry (next, bdbm_hlm_req_t, list);
		list_del (&item->list);
		bdbm_free (item);
		count++;
	}

	/* free & remove items from the free_list */
	list_for_each_safe (next, temp, &pool->free_list) {
		item = list_entry (next, bdbm_hlm_req_t, list);
		list_del (&item->list);
		bdbm_free (item);
		count++;
	}

	if (count != pool->pool_size) {
		bdbm_warning ("oops! count != pool->pool_size (%lld != %lld)",
			count, pool->pool_size);
	}

	/* free other stuff */
	bdbm_spin_lock_destory (&pool->lock);
	bdbm_free (pool);
}

bdbm_hlm_req_t* bdbm_hlm_reqs_pool_alloc_item (
	bdbm_hlm_reqs_pool_t* pool)
{
	struct list_head* pos = NULL;
	bdbm_hlm_req_t* item = NULL;

	bdbm_spin_lock (&pool->lock);

again:
	/* see if there are free items in the free_list */
	list_for_each (pos, &pool->free_list) {
		item = list_entry (pos, bdbm_hlm_req_t, list);
		break;
	}

	/* oops! there are no free items in the free-list */
	if (item == NULL) {
		int i = 0;
		/* add more items to the free-list */
		for (i = 0; i < DEFAULT_POOL_INC_SIZE; i++) {
			bdbm_hlm_req_t* item = NULL;
			if ((item = (bdbm_hlm_req_t*)bdbm_malloc (sizeof (bdbm_hlm_req_t))) == NULL) {
				bdbm_error ("bdbm_malloc () failed");
				goto fail;
			}
			list_add_tail (&item->list, &pool->free_list);
		}
		/* increase the size of the pool */
		pool->pool_size += DEFAULT_POOL_INC_SIZE;

		/* try it again */
		goto again;
	}

	if (item == NULL)
		goto fail;

	/* move it to the used_list */
	list_del (&item->list);
	list_add_tail (&item->list, &pool->used_list);

	bdbm_spin_unlock (&pool->lock);
	return item;

fail:

	bdbm_spin_unlock (&pool->lock);
	return NULL;
}

void bdbm_hlm_reqs_pool_free_item (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* item)
{
	bdbm_spin_lock (&pool->lock);
	list_del (&item->list);
	list_add_tail (&item->list, &pool->free_list);
	bdbm_spin_unlock (&pool->lock);
}

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

	/*bdbm_msg ("TRIM(NEW): LPA=%llu LEN=%llu (%llu %llu)", */
	/*hr->trim_lpa, hr->trim_len, br->bi_offset, br->bi_size);*/

	return 0;
}

static int __hlm_reqs_pool_create_rw_req (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* hr,
	bdbm_blkio_req_t* br)
{
	int64_t sec_start, sec_end, pg_start, pg_end;
	int64_t i = 0, j = 0, k = 0;
	int64_t hole = 0, bvec_cnt = 0, nr_llm_reqs;
	bdbm_flash_page_main_t* ptr_fm = NULL;
	bdbm_llm_req_t* ptr_lr = NULL;

	/* expand boundary sectors */
	sec_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(pool->map_unit));
	sec_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(pool->map_unit));

	pg_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);

	bdbm_bug_on (sec_start >= sec_end);

	/* build llm_reqs */
	nr_llm_reqs = BDBM_ALIGN_UP ((sec_end - sec_start), NR_KSECTORS_IN(pool->io_unit));
	nr_llm_reqs = nr_llm_reqs / NR_KSECTORS_IN(pool->io_unit);

	bdbm_bug_on (nr_llm_reqs > BDBM_BLKIO_MAX_VECS);

	ptr_lr = &hr->llm_reqs[0];

	for (i = 0; i < nr_llm_reqs; i++) {
		/* build mapping-units */
		ptr_fm = &ptr_lr->fmain;
		for (j = 0, hole = 0; j < pool->io_unit / pool->map_unit; j++) {
			/* build kernel-pages */
			ptr_lr->logaddr.lpa[j] = sec_start / NR_KSECTORS_IN(pool->map_unit);
			for (k = 0; k < NR_KPAGES_IN(pool->map_unit); k++) {
				uint64_t pg_off = sec_start / NR_KSECTORS_IN(KPAGE_SIZE);
				uint64_t kp_off = j * (pool->io_unit / pool->map_unit) + k;

				if (pg_off < pg_start) {
					ptr_fm->kp_stt[kp_off] = KP_STT_HOLE;
					ptr_fm->kp_ptr[kp_off] = ptr_fm->kp_pad[kp_off];
#ifdef DGB_POOL
					memset (ptr_fm->kp_ptr[kp_off], 0x00, 4096);
					if (br->bi_rw == REQTYPE_WRITE)
						bdbm_msg ("lpa=%llu(%llu) %p (%llx %llx %llx ...)", 
								ptr_lr->logaddr.lpa[j], kp_off, ptr_fm->kp_ptr[kp_off], 
								ptr_fm->kp_ptr[kp_off][0],
								ptr_fm->kp_ptr[kp_off][1],
								ptr_fm->kp_ptr[kp_off][2]);
#endif
					hole = 1;
				} else if (pg_off >= pg_end) { 
					ptr_fm->kp_stt[kp_off] = KP_STT_HOLE;
					ptr_fm->kp_ptr[kp_off] = ptr_fm->kp_pad[kp_off];
					memset (ptr_fm->kp_ptr[kp_off], 0x00, KERNEL_PAGE_SIZE);
#ifdef DGB_POOL
					if (br->bi_rw == REQTYPE_WRITE)
						bdbm_msg ("lpa=%llu(%llu) %p (%llx %llx %llx ...)", 
								ptr_lr->logaddr.lpa[j], kp_off, ptr_fm->kp_ptr[kp_off], 
								ptr_fm->kp_ptr[kp_off][0],
								ptr_fm->kp_ptr[kp_off][1],
								ptr_fm->kp_ptr[kp_off][2]);
#endif
					hole = 1;
				} else {
					bdbm_bug_on (bvec_cnt >= br->bi_bvec_cnt);

					ptr_fm->kp_stt[kp_off] = KP_STT_DATA;
					ptr_fm->kp_ptr[kp_off] = br->bi_bvec_ptr[bvec_cnt++]; /* assign actual data */
				}

				/* go to the next */
				sec_start += NR_KSECTORS_IN(KPAGE_SIZE);
				kp_off++;
			}
		}

		/*bdbm_msg ("%d", kp_off);*/

		/* decide the reqtype for llm_req */
		if (hole == 1 && br->bi_rw == REQTYPE_WRITE)
			ptr_lr->req_type = REQTYPE_RMW_READ;
		else
			ptr_lr->req_type = br->bi_rw;

		/* go to the next */
		ptr_lr->ptr_hlm_req = (void*)hr;
		ptr_lr++;
	}

	bdbm_bug_on (bvec_cnt != br->bi_bvec_cnt);

	/* intialize hlm_req */
	hr->req_type = br->bi_rw;
	bdbm_stopwatch_start (&hr->sw);
	hr->nr_llm_reqs = nr_llm_reqs;
	atomic64_set (&hr->nr_llm_reqs_done, 0);
	bdbm_mutex_lock (&hr->done);
	hr->blkio_req = (void*)br;
	hr->ret = 0;

	return 0;
}

int bdbm_hlm_reqs_pool_build_req (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* item,
	bdbm_blkio_req_t* r)
{
	/* create a hlm_req using a bio */
	if (r->bi_rw == REQTYPE_TRIM) {
		if (__hlm_reqs_pool_create_trim_req (pool, item, r) != 0) {
			bdbm_error ("__hlm_reqs_pool_create_trim_req () failed");
			return 1;
		}
	} else if (r->bi_rw == REQTYPE_READ || r->bi_rw == REQTYPE_WRITE) {
		if (__hlm_reqs_pool_create_rw_req (pool, item, r) != 0) {
			bdbm_error ("__hlm_reqs_pool_create_rw_req () failed");
			return 1;
		}
	} else {
		bdbm_error ("oops! invalid request type: (%llx)", r->bi_rw);
		return 1;
	}

	return 0;
}
