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
	/*hr->nr_mu = 0;*/
	hr->blkio_req = (void*)br;
	hr->ret = 0;

	if (sec_start < sec_end) {
		hr->trim_lpa = (sec_start) / NR_KSECTORS_IN(pool->map_unit);
		hr->trim_len = (sec_end - sec_start) / NR_KSECTORS_IN(pool->map_unit);
	} else {
		hr->trim_lpa = (sec_start) / NR_KSECTORS_IN(pool->map_unit);
		hr->trim_len = 0;
	}
	/*hr->nr_mu = hr->trim_len;*/
	bdbm_stopwatch_start (&hr->sw);

	/*bdbm_msg ("TRIM(NEW): LPA=%llu LEN=%llu (%llu %llu)", */
	/*hr->trim_lpa, hr->trim_len, br->bi_offset, br->bi_size);*/

	return 0;
}

#if 0
static int __hlm_reqs_pool_create_rw_req (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* hr,
	bdbm_blkio_req_t* br)
{
	bdbm_mu_t* ptr_mu;
	int64_t sec_start, sec_end;
	int64_t pg_start, pg_end;
	int64_t i = 0, j = 0, k = 0;

	/* init variables */
	hr->req_type = br->bi_rw;
	hr->nr_mu = 0;
	hr->blkio_req = (void*)br;
	hr->ret = 0;

	/* expand boundary sectors */
	sec_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(pool->map_unit));
	sec_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(pool->map_unit));

	pg_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KERNEL_PAGE_SIZE));
	pg_start /= NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KERNEL_PAGE_SIZE));
	pg_end /= NR_KSECTORS_IN(KPAGE_SIZE);

	bdbm_bug_on (sec_start >= sec_end);

	/*bdbm_msg ("br->bi_offset: %llu, br->bi_size: %llu", br->bi_offset, br->bi_size);*/
	/*bdbm_msg ("pg_start: %llu, pg_end: %llu", pg_start, pg_end);*/

	ptr_mu = &hr->mu[0];
	while (sec_start < sec_end) {
		int8_t hole = 0;

		/* see if the length of br is larger then expectation (TODO: it would
	 	 * be much better to dynamically increase # of LUs) */
		bdbm_bug_on (hr->nr_mu >= 32);

		/* ------------------- */
		/* build mapping-units */
		ptr_mu->lpa = sec_start / NR_KSECTORS_IN(pool->map_unit);
		for (j = 0; j < NR_KPAGES_IN(pool->map_unit); j++) {
			uint64_t pg_off = sec_start / NR_KSECTORS_IN(KPAGE_SIZE);

			/* ------------------- */
			/* build kernel-pages */
			if (pg_off < pg_start) {
				ptr_mu->kp_stt[j] = KP_STT_HOLE;
				ptr_mu->kp_ptr[j] = ptr_mu->kp_pad[j];
				hole = 1;
			} else if (pg_off >= pg_end) { 
				ptr_mu->kp_stt[j] = KP_STT_HOLE;
				ptr_mu->kp_ptr[j] = ptr_mu->kp_pad[j];
				hole = 1;
			} else {
				ptr_mu->kp_stt[j] = KP_STT_DATA;
				ptr_mu->kp_ptr[j] = br->bi_bvec_ptr[k];
				k++;
			}

			/* go to the next */
			sec_start += NR_KSECTORS_IN(KPAGE_SIZE);

			/* check error cases */
			bdbm_bug_on (k > br->bi_bvec_cnt);
		}

		/* decide the type of requests */
		if (hole == 1 && br->bi_rw == REQTYPE_WRITE)
			ptr_mu->req_type = REQTYPE_RMW_READ;

		/* go to the next */
		hr->nr_mu++;
		ptr_mu++;
	}

	/*
	bdbm_msg ("RW(NEW): LPA=%llu LEN=%llu (%llu %llu)",
			hr->mu[0].lpa, hr->nr_mu, br->bi_offset, br->bi_size);
	*/

	bdbm_stopwatch_start (&hr->sw);
	return 0;
}
#endif

static int __hlm_reqs_pool_create_rw_req (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* hr,
	bdbm_blkio_req_t* br)
{
	int64_t sec_start;
	int64_t sec_end;
	int64_t pg_start;
	int64_t pg_end;
	int64_t i = 0, bvec_cnt = 0, nr_llm_reqs;
	bdbm_llm_req2_t* ptr_lr = NULL;
	bdbm_flash_page_main_t* ptr_fm = NULL;

	/* expand boundary sectors */
	sec_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(pool->map_unit));
	sec_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(pool->map_unit));

	pg_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);

	bdbm_bug_on (sec_start >= sec_end);

	/* build llm_reqs */
	nr_llm_reqs = BDBM_ALIGN_UP ((sec_end - sec_start), NR_KSECTORS_IN(pool->io_unit));
	nr_llm_reqs = nr_llm_reqs / NR_KSECTORS_IN(pool->io_unit);

	ptr_lr = &hr->llm_reqs[0];

	for (i = 0; i < nr_llm_reqs; i++) {
		int hole = 0, j = 0;
		ptr_fm = &ptr_lr->fmain;

		/*bdbm_mu_t* ptr_mu = &ptr_lr->mu[0];*/

		/* build mapping-units */
		for (j = 0; j < pool->io_unit / pool->map_unit; j++) {
			int k = 0;
			/* build kernel-pages */
			/*ptr_mu->lpa = sec_start / NR_KSECTORS_IN(pool->map_unit);*/
			ptr_lr->logaddr.lpa[j] = sec_start / NR_KSECTORS_IN(pool->map_unit);
			for (k = 0; k < NR_KPAGES_IN(pool->map_unit); k++) {
				uint64_t pg_off = sec_start / NR_KSECTORS_IN(KPAGE_SIZE);
				uint64_t kp_off = j * (pool->io_unit / pool->map_unit) + k;

				if (pg_off < pg_start) {
					ptr_fm->kp_stt[kp_off] = KP_STT_HOLE;
					ptr_fm->kp_ptr[kp_off] = ptr_fm->kp_pad[k];
					hole = 1;
				} else if (pg_off >= pg_end) { 
					ptr_fm->kp_stt[kp_off] = KP_STT_HOLE;
					ptr_fm->kp_ptr[kp_off] = ptr_fm->kp_pad[k];
					hole = 1;
				} else {
					ptr_fm->kp_stt[kp_off] = KP_STT_DATA;
					ptr_fm->kp_ptr[kp_off] = br->bi_bvec_ptr[bvec_cnt++]; /* assign actual data */
				}

				/* go to the next */
				sec_start += NR_KSECTORS_IN(KPAGE_SIZE);

				/* check error cases */
				bdbm_bug_on (bvec_cnt > br->bi_bvec_cnt);
			}
		}

		if (hole == 1 && br->bi_rw == REQTYPE_WRITE)
			ptr_lr->req_type = REQTYPE_RMW_READ;
		else
			ptr_lr->req_type = br->bi_rw;
		ptr_lr++;
	}

	/* intialize hlm_req */
	hr->req_type = br->bi_rw;
	hr->blkio_req = (void*)br;
	hr->nr_llm_reqs = nr_llm_reqs;
	hr->ret = 0;
	bdbm_stopwatch_start (&hr->sw);

	return 0;
}


#if 0
static int __hlm_reqs_pool_create_rw_req (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* hr,
	bdbm_blkio_req_t* br)
{
	int64_t sec_start, sec_end;
	int64_t pg_start, pg_end;

	int64_t f = 0, m = 0, k = 0, bvec_cnt = 0, nr_llm_reqs;
	bdbm_llm_req2_t* ptr_llm_reqs = NULL;

	/* expand boundary sectors */
	sec_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(pool->map_unit));
	sec_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(pool->map_unit));

	pg_start = BDBM_ALIGN_DOWN (br->bi_offset, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);
	pg_end = BDBM_ALIGN_UP (br->bi_offset + br->bi_size, NR_KSECTORS_IN(KPAGE_SIZE)) / NR_KSECTORS_IN(KPAGE_SIZE);

	bdbm_bug_on (sec_start >= sec_end);

	/* build llm_reqs */
	nr_llm_reqs = BDBM_ALIGN_UP ((sec_end - sec_start), NR_KSECTORS_IN(pool->io_unit));
	nr_llm_reqs = nr_llm_reqs / NR_KSECTORS_IN(pool->io_unit);
	ptr_llm_reqs = &hr->llm_reqs[0];

	for (f = 0; f < nr_llm_reqs; f++) {
		int hole = 0;
		bdbm_mu_t* ptr_mu = &ptr_llm_reqs->mu[0];

		/* build mapping-units */
		for (m = 0; m < pool->io_unit / pool->map_unit; m++) {
			/* build kernel-pages */
			ptr_mu->lpa = sec_start / NR_KSECTORS_IN(pool->map_unit);
			for (k = 0; k < NR_KPAGES_IN(pool->map_unit); k++) {
				uint64_t pg_off = sec_start / NR_KSECTORS_IN(KPAGE_SIZE);

				if (pg_off < pg_start) {
					ptr_mu->kp_stt[k] = KP_STT_HOLE;
					ptr_mu->kp_ptr[k] = ptr_mu->kp_pad[k];
					hole = 1;
				} else if (pg_off >= pg_end) { 
					ptr_mu->kp_stt[k] = KP_STT_HOLE;
					ptr_mu->kp_ptr[k] = ptr_mu->kp_pad[k];
					hole = 1;
				} else {
					ptr_mu->kp_stt[k] = KP_STT_DATA;
					ptr_mu->kp_ptr[k] = br->bi_bvec_ptr[bvec_cnt++];
				}

				/* go to the next */
				sec_start += NR_KSECTORS_IN(KPAGE_SIZE);

				/* check error cases */
				bdbm_bug_on (bvec_cnt > br->bi_bvec_cnt);
			}
			ptr_mu++;
		}
		if (hole == 1 && br->bi_rw == REQTYPE_WRITE)
			ptr_llm_reqs->req_type = REQTYPE_RMW_READ;
		else
			ptr_llm_reqs->req_type = br->bi_rw;
		ptr_llm_reqs++;
	}

	/* intialize hlm_req */
	hr->req_type = br->bi_rw;
	hr->blkio_req = (void*)br;
	hr->nr_llm_reqs = nr_llm_reqs;
	hr->ret = 0;
	bdbm_stopwatch_start (&hr->sw);

	return 0;
}
#endif

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

