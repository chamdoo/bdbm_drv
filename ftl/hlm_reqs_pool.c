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

	/*
	bdbm_msg ("TRIM(NEW): LPA=%llu LEN=%llu (%llu %llu)", 
		hr->lpa, hr->len, br->bi_offset, br->bi_size);
	*/

	return 0;
}

#if 0
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
	bdbm_bug_on (sec_start >= sec_end);

	pg_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	bdbm_bug_on (pg_start >= pg_end);

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
					hole = 1;
				} else if (pg_off >= pg_end) { 
					ptr_fm->kp_stt[kp_off] = KP_STT_HOLE;
					ptr_fm->kp_ptr[kp_off] = ptr_fm->kp_pad[kp_off];
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

		/* decide the reqtype for llm_req */
		if (hole == 1 && br->bi_rw == REQTYPE_WRITE) {
			ptr_lr->req_type = REQTYPE_RMW_READ;
		} else
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
#endif

static void __hlm_reqs_pool_reset_fmain (
	bdbm_flash_page_main_t* fmain)
{
	int i = 0;
	fmain->sz = 0;
	while (i < 32) {
		fmain->kp_stt[i] = KP_STT_HOLE;
		fmain->kp_ptr[i] = fmain->kp_pad[i];
		i++;
	}
}

static int __hlm_reqs_pool_create_write_req (
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
	bdbm_bug_on (sec_start >= sec_end);

	pg_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	bdbm_bug_on (pg_start >= pg_end);

	/* build llm_reqs */
	nr_llm_reqs = BDBM_ALIGN_UP ((sec_end - sec_start), NR_KSECTORS_IN(pool->io_unit)) / NR_KSECTORS_IN(pool->io_unit);
	bdbm_bug_on (nr_llm_reqs > BDBM_BLKIO_MAX_VECS);

#if 0
	bdbm_msg ("[WRITE] Len: %llu -----------------------------------------", nr_llm_reqs);
#endif

	ptr_lr = &hr->llm_reqs[0];
	for (i = 0; i < nr_llm_reqs; i++) {
		ptr_lr->logaddr.sz = 0;

		ptr_fm = &ptr_lr->fmain;
		__hlm_reqs_pool_reset_fmain (ptr_fm);

		/* build mapping-units */
		for (j = 0, hole = 0; j < pool->io_unit / pool->map_unit; j++) {
			/* build kernel-pages */
			ptr_lr->logaddr.lpa[j] = sec_start / NR_KSECTORS_IN(pool->map_unit);
			ptr_lr->logaddr.sz++;

			for (k = 0; k < NR_KPAGES_IN(pool->map_unit); k++) {
				uint64_t pg_off = sec_start / NR_KSECTORS_IN(KPAGE_SIZE);

				if (pg_off >= pg_start && pg_off < pg_end) {
					bdbm_bug_on (bvec_cnt >= br->bi_bvec_cnt);
					ptr_fm->kp_stt[ptr_fm->sz] = KP_STT_DATA;
					ptr_fm->kp_ptr[ptr_fm->sz] = br->bi_bvec_ptr[bvec_cnt++]; /* assign actual data */
					/* TEMP */
					/*bdbm_msg (" - LPA: %llu (%llu)", ptr_lr->logaddr.lpa[j], ptr_fm->sz);*/
					/* TEMP */
				} else {
					hole = 1;
					/* TEMP */
					/*bdbm_msg (" - LPA: %llu (%llu) - HOLE", ptr_lr->logaddr.lpa[j], ptr_fm->sz);*/
					/*hole = 0;*/
					/* TEMP */
				}

				/* go to the next */
				sec_start += NR_KSECTORS_IN(KPAGE_SIZE);
				ptr_fm->sz++;

				/* TEMP */
				/*bdbm_bug_on (k > 0);*/
				/* TEMP */
			}

			/* TEMP - NEW */
			/*if (sec_start >= sec_end)*/
			/*break;*/
			/* TMEP - NEW */
		}

		/* decide the reqtype for llm_req */
		if (hole == 1 && br->bi_rw == REQTYPE_WRITE) {
			ptr_lr->req_type = REQTYPE_RMW_READ;
			/*bdbm_bug_on (1);*/
			/* TEMP */
			/*exit (-1);*/
			/* TEMP */
		} else
			ptr_lr->req_type = br->bi_rw;

		/*bdbm_bug_on (ptr_fm->sz != 32);*/
		/*bdbm_bug_on (ptr_lr->logaddr.sz != 32);*/

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

static int __hlm_reqs_pool_create_read_req (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* hr,
	bdbm_blkio_req_t* br)
{
	int64_t pg_start, pg_end;
	int64_t i = 0, j = 0, k = 0;
	int64_t offset = 0, bvec_cnt = 0, nr_llm_reqs;
	bdbm_llm_req_t* ptr_lr = NULL;

	pg_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	bdbm_bug_on (pg_start >= pg_end);

	/* build llm_reqs */
	nr_llm_reqs = pg_end - pg_start;

	/* TEMP */
#if 0
	bdbm_msg ("[READ] Len: %llu -----------------------------------------", nr_llm_reqs);
#endif
	/* TEMP */

	ptr_lr = &hr->llm_reqs[0];
	for (i = 0; i < nr_llm_reqs; i++) {
		__hlm_reqs_pool_reset_fmain (&ptr_lr->fmain);

		offset = pg_start % NR_KPAGES_IN(pool->map_unit);
		/*offset = 0;*/

		ptr_lr->fmain.kp_stt[offset] = KP_STT_DATA;
		ptr_lr->fmain.kp_ptr[offset] = br->bi_bvec_ptr[bvec_cnt++];
		ptr_lr->fmain.sz = 1;

		ptr_lr->req_type = br->bi_rw;
		ptr_lr->logaddr.lpa[0] = pg_start / NR_KPAGES_IN(pool->map_unit);
		ptr_lr->logaddr.ofs = offset;
		ptr_lr->logaddr.sz = 1;
		ptr_lr->ptr_hlm_req = (void*)hr;

		/* TEMP */
		/*bdbm_msg (" - LPA: %llu (%llu)", ptr_lr->logaddr.lpa[0], offset);*/
		/* TEMP */

		/* go to the next */
		pg_start++;
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
	bdbm_hlm_req_t* hr,
	bdbm_blkio_req_t* br)
{
	int ret = 1;

	/* create a hlm_req using a bio */
	if (br->bi_rw == REQTYPE_TRIM) {
		ret = __hlm_reqs_pool_create_trim_req (pool, hr, br);
	} else if (br->bi_rw == REQTYPE_WRITE) {
		ret = __hlm_reqs_pool_create_write_req (pool, hr, br);
	} else if (br->bi_rw == REQTYPE_READ) {
		//ret = __hlm_reqs_pool_create_write_req (pool, hr, br);
		ret = __hlm_reqs_pool_create_read_req (pool, hr, br);
	}

	/* TEMP */
	/*
	if (br->bi_rw == REQTYPE_READ) {
		bdbm_hlm_req_t* hlm_test = bdbm_hlm_reqs_pool_alloc_item (pool);
		__hlm_reqs_pool_create_read_req (pool, hlm_test, br);
		bdbm_mutex_unlock (&hlm_test->done);
		bdbm_hlm_reqs_pool_free_item (pool, hlm_test);
	}
	*/
	/* TEMP */

	/* are there any errors? */
	if (ret != 0) {
		bdbm_error ("oops! invalid request type: (%llx)", br->bi_rw);
		return 1;
	}

	return 0;
}

#if 0
int bdbm_hlm_reqs_pool_rebuild_req (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t** hr)
{
	bdbm_llm_req_t* new_lr = NULL;
	bdbm_hlm_req_t* new_hr = NULL;
	bdbm_llm_req_t* old_lr = NULL;
	bdbm_hlm_req_t* old_hr = *hr;
	int32_t i = 0, offset = 0;

	/* only supports a read operation */
	if (old_hr->req_type != REQTYPE_READ)
		return 0;

	/* create new hlm_req */
	if ((new_hr = bdbm_hlm_reqs_pool_alloc_item (pool)) == NULL) {
		bdbm_error ("bdbm_hlm_reqs_pool_alloc_item () failed");
		return 1;
	}

	/*
		ptr_lr->fmain.kp_stt[offset] = KP_STT_DATA;
		ptr_lr->fmain.kp_ptr[offset] = br->bi_bvec_ptr[bvec_cnt++];
		ptr_lr->fmain.sz = 1;

		ptr_lr->req_type = br->bi_rw;
		ptr_lr->logaddr.lpa[0] = pg_start / NR_KPAGES_IN(pool->map_unit);
		ptr_lr->logaddr.ofs = offset;
		ptr_lr->logaddr.sz = 1;
		ptr_lr->ptr_hlm_req = (void*)hr;
	*/

	new_lr = &new_hr->llm_reqs[0];

	/* it rebuilds new hlm_req using old hlm_req */
	bdbm_hlm_for_each_llm_req (old_lr, old_hr, i) {
		bdbm_bug_on (old_lr->req_type != REQTYPE_READ);

		offset = old_lr->logaddr.ofs;

		bdbm_bug_on (old_lr->fmain.kp_stt[offset] != KP_STT_DATA);
	
		/* copy old_lr => new_lr */
		new_lr->fmain.kp_stt[offset] = old_lr->fmain.kp_stt[offset];
		new_lr->fmain.kp_ptr[offset] = old_lr->fmain.kp_ptr[offset];
		new_lr->fmain.sz = 1;

		new_lr->req_type = old_lr->req_type;
		new_lr->logaddr.lpa[0] = old_lr->logaddr.lpa[0];
		new_lr->logaddr.ofs = old_lr->logaddr.ofs;
		new_lr->logaddr.sz = old_lr->logaddr.sz;
		new_lr->ptr_hlm_req = old_lr->ptr_hlm_req;
	}

	/* intialize new hlm_req */
	new_hr->req_type = old_hr->req_type;
	bdbm_stopwatch_start (&new_hr->sw);
	new_hr->nr_llm_reqs = old_hr->nr_llm_reqs;
	atomic64_set (&new_hr->nr_llm_reqs_done, 0);
	bdbm_mutex_lock (&new_hr->done);
	new_hr->blkio_req = (void*)old_hr->blkio_req;
	new_hr->ret = 0;

	/* remove old hlm_req */
	bdbm_mutex_unlock (&old_hr->done);
	bdbm_hlm_reqs_pool_free_item (pool, old_hr);

	/* change the pointer */
	*hr = new_hr;

	return 0;
}

int bdbm_hlm_reqs_pool_rebuild_req (
	bdbm_hlm_req_t* hr)
{
	bdbm_llm_req_t* prev_lr = NULL;
	bdbm_llm_req_t* next_lr = NULL;
	int i = 0, offset = 0, nr_llm_req = 0;

	/* only supports a read operation */
	if (hr->req_type != REQTYPE_READ)
		return 0;

	/* it rebuilds new hlm_req using old hlm_req */
	bdbm_hlm_for_each_llm_req (next_lr, hr, i) {
		/* is it first? */
		if (prev_lr == NULL) {
			prev_lr = next_lr;
			nr_llm_req++;
			continue;
		}

		/* has data */
		if (next_lr->fmain.sz == 0)
			continue;
		
		/* has the same LPA? */
		if (prev_lr->logaddr.lpa[0] != next_lr->logaddr.lpa[0]) {
			prev_lr = next_lr;
			nr_llm_req++;
			continue;
		}

		/* has the same physical LPA? */
		if (memcmp (&prev_lr->phyaddr, &next_lr->phyaddr, sizeof (bdbm_phyaddr_t)) != 0) {
			prev_lr = next_lr;
			nr_llm_req++;
			continue;
		}

		/* kp is empty? */
		offset = next_lr->logaddr.ofs;
		bdbm_bug_on (next_lr->fmain.kp_stt[offset] != KP_STT_DATA);
		if (prev_lr->fmain.kp_stt[offset] == KP_STT_DATA) {
			prev_lr = next_lr;
			nr_llm_req++;
			continue;
		}

		/* ok! next_lr can be merged with prev_lr */
		prev_lr->fmain.kp_stt[offset] = next_lr->fmain.kp_stt[offset];
		prev_lr->fmain.kp_ptr[offset] = next_lr->fmain.kp_ptr[offset];
		prev_lr->fmain.sz++;

		/* */
		next_lr->fmain.kp_stt[offset] = KP_STT_HOLE;
		next_lr->fmain.kp_ptr[offset] = next_lr->fmain.kp_pad[offset];
		next_lr->fmain.sz--;

		next_lr->logaddr.lpa[0] = -1;


#if 0
		bdbm_bug_on (old_lr->req_type != REQTYPE_READ);

		offset = old_lr->logaddr.ofs;

		bdbm_bug_on (old_lr->fmain.kp_stt[offset] != KP_STT_DATA);
	
		/* copy old_lr => new_lr */
		new_lr->fmain.kp_stt[offset] = old_lr->fmain.kp_stt[offset];
		new_lr->fmain.kp_ptr[offset] = old_lr->fmain.kp_ptr[offset];
		new_lr->fmain.sz = 1;

		new_lr->req_type = old_lr->req_type;
		new_lr->logaddr.lpa[0] = old_lr->logaddr.lpa[0];
		new_lr->logaddr.ofs = old_lr->logaddr.ofs;
		new_lr->logaddr.sz = old_lr->logaddr.sz;
		new_lr->ptr_hlm_req = old_lr->ptr_hlm_req;
#endif
	}

	return 0;
}
#endif

#if 0
static void __bdbm_hlm_reqs_pool_copy_hlm (
	bdbm_hlm_req_t* hr_src,
	bdbm_hlm_req_t* hr_dst)
{
	bdbm_llm_req_t* lr_src = NULL;
	bdbm_llm_req_t* lr_dst = NULL;
	int i = 0, j = 0;

	hr_dst->req_type = hr_src->req_type;
	hr_dst->sw = hr_src->sw;

	if (hr_src->req_type == REQTYPE_TRIM) {
		hr_dst->lpa = hr_src->lpa;
		hr_dst->len = hr_src->len;
	} else {
		hr_dst->nr_llm_reqs = hr_src->nr_llm_reqs;
		atomic64_set (&hr_dst->nr_llm_reqs_done, 0);

		lr_dst = &hr_dst->llm_reqs[0];
		bdbm_hlm_for_each_llm_req (lr_src, hr_src, i) {
			/* copy llm */
			lr_dst->req_type = lr_src->req_type;
			lr_dst->ret = lr_src->ret;
			lr_dst->ptr_hlm_req = lr_src->ptr_hlm_req;
			lr_dst->ptr_qitem = lr_src->ptr_qitem;
			lr_dst->logaddr = lr_src->logaddr;
			lr_dst->phyaddr = lr_src->phyaddr;
			lr_dst->phyaddr_src = lr_src->phyaddr_src;
			lr_dst->phyaddr_dst = lr_src->phyaddr_dst;

			/* copy fmain */
			lr_dst->fmain.sz = lr_src->fmain.sz;
			for (j = 0; j < 32; j++) {
				lr_dst->fmain.kp_stt[j] = lr_src->fmain.kp_stt[j];
				if (lr_src->fmain.kp_stt[j] == KP_STT_DATA)
					lr_dst->fmain.kp_ptr[j] = lr_src->fmain.kp_ptr[j];
				else
					lr_dst->fmain.kp_ptr[j] = lr_dst->fmain.kp_pad[j];
			}
			lr_dst++;
		}
		// bdbm_mutex_init (&item->done); /* skip it */
	}

	hr_dst->blkio_req = hr_src->blkio_req;
	hr_dst->ret = 0;
}

static void __bdbm_hlm_reqs_pool_reset_hlm (
	bdbm_hlm_req_t* hr)
{
	bdbm_llm_req_t* lr = NULL;
	int i = 0;

	bdbm_hlm_for_each_llm_req (lr, hr, i) {
		/* reset llm */
		lr->req_type = -1;
		lr->ret = 0;
		lr->ptr_hlm_req = NULL;
		lr->ptr_qitem = NULL;

		/* reset fmain */
		__hlm_reqs_pool_reset_fmain (&lr->fmain);
	}

	/* reset hlm */
	hr->nr_llm_reqs = 0;
	atomic64_set (&hr->nr_llm_reqs_done, 0);
	hr->ret = 0;
	hr->req_type = 0;
}

static int __bdbm_hlm_reqs_pool_rebuild_req (
	bdbm_hlm_req_t* hr,
	bdbm_hlm_req_t* tmp_hr)
{
	return 0;
}

int bdbm_hlm_reqs_pool_rebuild_req (
	bdbm_hlm_req_t* hr,
	bdbm_hlm_req_t* tmp_hr)
{
	int ret;

	/* only supports a read operation */
	if (hr->req_type != REQTYPE_READ)
		return 0;
	
	/* copy hr to tmp_hr */
	__bdbm_hlm_reqs_pool_copy_hlm (hr, tmp_hr);

	/* reset hr */
	__bdbm_hlm_reqs_pool_reset_hlm (hr);

	/* copy tmp_hr to hr */
	__bdbm_hlm_reqs_pool_copy_hlm (tmp_hr, hr);

	/* rebuild hr */
	ret = __bdbm_hlm_reqs_pool_rebuild_req (hr, tmp_hr);

	return ret;
}
#endif



