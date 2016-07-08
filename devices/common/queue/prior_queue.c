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

#if defined (KERNEL_MODE)
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>

#elif defined (USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "bdbm_drv.h"
#include "debug.h"
#include "umemory.h"
#include "prior_queue.h"


/*static uint64_t max_queue_items = 0;*/

static uint64_t get_highest_priority_tag (
	bdbm_prior_queue_t* mq, 
	uint64_t lpa)
{
	bdbm_prior_lpa_item_t* q = NULL;

	HASH_FIND_INT (mq->hash_lpa, &lpa, q);
	if (q) {
		//bdbm_msg("   get_highest_tag: lpa: %llu, tag: %llu", lpa, q->cur_tag);
		return q->cur_tag;
	}
	//bdbm_msg("tjkim highest_priority_tag -1!!!!");
	return -1;
}

static void remove_highest_priority_tag (
	bdbm_prior_queue_t* mq, 
	uint64_t lpa)
{
	bdbm_prior_lpa_item_t* q = NULL;

	HASH_FIND_INT (mq->hash_lpa, &lpa, q);
	if (q && q->lpa == lpa) {
		//bdbm_msg("   remove_highest_tag: lpa: %llu, cur_tag: %llu, max_tag: %llu", lpa, q->cur_tag, q->max_tag);
		if (q->max_tag == q->cur_tag) {
			HASH_DEL (mq->hash_lpa, q);
			bdbm_free_atomic (q);
			q = NULL;
		} else if (q->max_tag > q->cur_tag)
			q->cur_tag++;
		else {
			bdbm_error ("oops!!!");
			bdbm_bug_on (1);
		}
	} 
	else {
		//bdbm_msg("   remove_highest_tag failed");
	}
}

static uint64_t get_new_priority_tag (
	bdbm_prior_queue_t* mq, 
	uint64_t lpa)
{
	bdbm_prior_lpa_item_t* q = NULL;

	HASH_FIND_INT (mq->hash_lpa, &lpa, q);
	if (q == NULL) {
		if ((q = (bdbm_prior_lpa_item_t*)bdbm_malloc_atomic 
				(sizeof (bdbm_prior_lpa_item_t))) == NULL) {
			bdbm_error ("bdbm_malloc_atomic failed");
			bdbm_bug_on (1);
		}
		q->lpa = lpa;
		q->max_tag = 1;
		q->cur_tag = 1;
		HASH_ADD_INT (mq->hash_lpa, lpa, q);
	} else {
		q->max_tag++;
		//bdbm_msg("    get_new_tag++: lpa: %llu, q->cur_tag: %llu, q->max_tag: %llu", lpa, q->cur_tag, q->max_tag);
	}

	return q->max_tag;
}

bdbm_prior_queue_t* bdbm_prior_queue_create (
	uint64_t nr_queues, 
	int64_t max_size)
{
	bdbm_prior_queue_t* mq;
	uint64_t loop;

	/* create a private structure */
	if ((mq = bdbm_malloc_atomic (sizeof (bdbm_prior_queue_t))) == NULL) {
		bdbm_msg ("bdbm_malloc_alloc failed");
		return NULL;
	}
	mq->nr_queues = nr_queues;
	mq->max_size = max_size;
	mq->qic = 0;
	//bdbm_spin_lock_init (&mq->lock);
	bdbm_init_completion (mq->lock);

	/* create linked-lists */
	if ((mq->qlh = bdbm_malloc_atomic (sizeof (struct list_head) * mq->nr_queues)) == NULL) {
		bdbm_msg ("bdbm_malloc_alloc failed");
		bdbm_free_atomic (mq);
		return NULL;
	}
	for (loop = 0; loop < mq->nr_queues; loop++)
		INIT_LIST_HEAD (&mq->qlh[loop]);

	/* create hash */
	mq->hash_lpa = NULL;

	return mq;
}

/* NOTE: it must be called when mq is empty. */
void bdbm_prior_queue_destroy (bdbm_prior_queue_t* mq)
{
	bdbm_prior_lpa_item_t *c, *tmp;

	if (mq == NULL)
		return;

	HASH_ITER (hh, mq->hash_lpa, c, tmp) {
		bdbm_warning ("hmm.. there are still some items in the hash table");
		HASH_DEL (mq->hash_lpa, c);
		bdbm_free_atomic (c);
	}
	bdbm_free_atomic (mq->qlh);
	bdbm_free_atomic (mq);
}

uint8_t bdbm_prior_queue_enqueue (
	bdbm_prior_queue_t* mq, 
	uint64_t qid, 
	uint64_t lpa, 
	void* req)
{
	uint32_t ret = 1;
	//unsigned long flags;

	if (qid >= mq->nr_queues) {
		bdbm_error ("qid is invalid (%llu)", qid);
		return 1;
	}

	//bdbm_spin_lock_irqsave (&mq->lock, flags);
	bdbm_wait_for_completion(mq->lock);

	if (mq->max_size == INFINITE_PRIOR_QUEUE || mq->qic < mq->max_size) {
		bdbm_prior_queue_item_t* q = NULL;
		if ((q = bdbm_malloc_atomic (sizeof (bdbm_prior_queue_item_t))) == NULL) {
			bdbm_error ("bdbm_malloc_atomic failed");
			bdbm_bug_on (1);
		} else {
			q->tag = get_new_priority_tag (mq, lpa);
			q->lpa = lpa;
			q->lock = 0;
			q->ptr_req = (void*)req;
			list_add_tail (&q->list, &mq->qlh[qid]);	/* add to tail */
			mq->qic++;
			ret = 0;
			//bdbm_msg("                    lpa: %llu, tag: %llu", q->lpa, q->tag);
			/*
			if (mq->qic > max_queue_items) {
				max_queue_items = mq->qic;
				bdbm_msg ("max queue items: %llu", max_queue_items);
			}
			*/

			//tjkim
			/*
			if(q->tag > 1) {
				bdbm_msg("  existing lpa: lpa: %llu, tag: %llu", q->lpa, q->tag);
			}
			*/
		}
	}
	//bdbm_spin_unlock_irqrestore (&mq->lock, flags);
	bdbm_complete(mq->lock);

	return ret;
}

uint8_t bdbm_prior_queue_is_empty (
	bdbm_prior_queue_t* mq, 
	uint64_t qid)
{
	//unsigned long flags;
	struct list_head* pos = NULL;
	bdbm_prior_queue_item_t* q = NULL;
	uint8_t ret = 1;

	//bdbm_spin_lock_irqsave (&mq->lock, flags);
	bdbm_wait_for_completion(mq->lock);
	if (mq->qic > 0) {
		list_for_each (pos, &mq->qlh[qid]) {
			q = list_entry (pos, bdbm_prior_queue_item_t, list);
			if (q && q->lock == 0)
				break;
			q = NULL;
			break; /* [CAUSION] it could incur a dead-lock problem */
		}
		if (q != NULL)
			ret = 0;
	}
	//bdbm_spin_unlock_irqrestore (&mq->lock, flags);
	bdbm_complete(mq->lock);

	return ret;
}

void* bdbm_prior_queue_dequeue (
	bdbm_prior_queue_t* mq, 
	uint64_t qid,
	bdbm_prior_queue_item_t** oq)
{
	//unsigned long flags;
	struct list_head* pos = NULL;
	bdbm_prior_queue_item_t* q = NULL;
	void* req = NULL;

	/*
	bdbm_stopwatch_t deq_sw;
	uint64_t elapsed_time = 0;
	*/

	//bdbm_spin_lock_irqsave (&mq->lock, flags);
	bdbm_wait_for_completion(mq->lock);
	//bdbm_msg("    deque start punit: %llu", qid);
	if (mq->qic > 0) {
		//bdbm_stopwatch_start(&deq_sw);

		list_for_each (pos, &mq->qlh[qid]) {
			if ((q = list_entry (pos, bdbm_prior_queue_item_t, list))) {
				if (q->lock == 0) {
					uint64_t highest_tag = get_highest_priority_tag (mq, q->lpa);
					/*
					if (highest_tag != 1)
						bdbm_msg("tjkim highest_tag: %llu, cur_tag: %llu, lpa: %llu", highest_tag, q->tag, q->lpa);
					*/
					if (highest_tag == q->tag)
						break;
				} 
			}
			q = NULL;
			break; /* [CAUSION] it could incur a dead-lock problem */
		}
		if (q != NULL) {
			q->lock = 1;	/* mark it use */
			req = q->ptr_req;
			*oq= q;
		}
		/*
		else {
			pos = NULL;
			bdbm_msg(" deque failed at punit %llu start", qid);
			list_for_each (pos, &mq->qlh[qid]) {
				if ((q = list_entry (pos, bdbm_prior_queue_item_t, list))) {
					bdbm_msg(" ** lpa: %llu, tag: %llu", q->lpa, q->tag);
				}
			}
			bdbm_msg(" deque failed end");
		}
		*/

		/*
		elapsed_time = bdbm_stopwatch_get_elapsed_time_us(&deq_sw);
		if(elapsed_time > 1000000)
			bdbm_msg("punit: %llu, deque elapsed time: %llu", qid, elapsed_time);
		*/
	}
	//bdbm_spin_unlock_irqrestore (&mq->lock, flags);
	bdbm_complete(mq->lock);

	return req;
}

uint8_t bdbm_prior_queue_remove (
	bdbm_prior_queue_t* mq, 
	bdbm_prior_queue_item_t* q)
{
	//unsigned long flags;

	//bdbm_spin_lock_irqsave (&mq->lock, flags);
	bdbm_wait_for_completion(mq->lock);

	if (q) {
		remove_highest_priority_tag (mq, q->lpa);
		list_del (&q->list);
		bdbm_free_atomic (q);
		mq->qic--;
		/*bdbm_msg ("[QUEUE] # of items in queue = %llu", mq->qic);*/
	}
	//bdbm_spin_unlock_irqrestore (&mq->lock, flags);
	bdbm_complete(mq->lock);

	return 0;
}

uint8_t bdbm_prior_queue_move (
	bdbm_prior_queue_t* mq, 
	uint64_t qid,
	bdbm_prior_queue_item_t* q)
{
	//unsigned long flags;

	//bdbm_spin_lock_irqsave (&mq->lock, flags);
	bdbm_wait_for_completion(mq->lock);
	if (q) {
		list_del (&q->list); /* remove from the list */
		q->lock = 0;
		list_add (&q->list, &mq->qlh[qid]);	/* add to tail */
	}
	//bdbm_spin_unlock_irqrestore (&mq->lock, flags);
	bdbm_complete(mq->lock);

	return 0;
}

uint8_t bdbm_prior_queue_is_full (bdbm_prior_queue_t* mq)
{
	uint8_t ret = 0;
	//unsigned long flags;

	//bdbm_spin_lock_irqsave (&mq->lock, flags);
	bdbm_wait_for_completion(mq->lock);
	if (mq->max_size != INFINITE_PRIOR_QUEUE) {
		if (mq->qic > mq->max_size) {
 			bdbm_error ("oops!!!");
			bdbm_bug_on (mq->qic > mq->max_size);
		}
		if (mq->qic == mq->max_size)
			ret = 1;
	}
	//bdbm_spin_unlock_irqrestore (&mq->lock, flags);
	bdbm_complete(mq->lock);

	return ret;
}

uint8_t bdbm_prior_queue_is_all_empty (bdbm_prior_queue_t* mq)
{
	uint8_t ret = 0;
	//unsigned long flags;

	if (mq == NULL)
		return 1;

	//bdbm_spin_lock_irqsave (&mq->lock, flags);
	bdbm_wait_for_completion(mq->lock);
	if (mq->qic == 0)
		ret = 1;	/* q is empty */
	//bdbm_spin_unlock_irqrestore (&mq->lock, flags);
	bdbm_complete(mq->lock);

	return ret;
}

uint64_t bdbm_prior_queue_get_nr_items (bdbm_prior_queue_t* mq)
{
	uint64_t nr_items = 0;
	//unsigned long flags;

	//bdbm_spin_lock_irqsave (&mq->lock, flags);
	bdbm_wait_for_completion(mq->lock);
	nr_items = mq->qic;
	//bdbm_spin_unlock_irqrestore (&mq->lock, flags);
	bdbm_complete(mq->lock);

	return nr_items;
}
