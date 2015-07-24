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
#include "hlm_nobuf.h"
#include "hlm_buf.h"
#include "uthread.h"

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"
#include "queue/queue.h"

/*#define USE_ORIGINAL*/


/* interface for hlm_buf */
struct bdbm_hlm_inf_t _hlm_buf_inf = {
	.ptr_private = NULL,
	.create = hlm_buf_create,
	.destroy = hlm_buf_destroy,
	.make_req = hlm_buf_make_req,
	.end_req = hlm_buf_end_req,
};


/* data structures for hlm_buf */
struct bdbm_hlm_buf_private {
	struct bdbm_ftl_inf_t* ptr_ftl_inf;	/* for hlm_nobuff (it must be on top of this structure) */

	/* for thread management */
	struct bdbm_queue_t* q;
#ifdef USE_ORIGINAL
	bdbm_mutex hlm_thread_done;
	struct task_struct* hlm_thread;
	wait_queue_head_t hlm_wq;
#else
	bdbm_thread_t* hlm_thread;
#endif
};


/* kernel thread for _llm_q */
#ifdef USE_ORIGINAL
int __hlm_buf_thread (void* arg)
{
	struct bdbm_drv_info* bdi = (struct bdbm_drv_info*)arg;
	struct bdbm_hlm_buf_private* p = (struct bdbm_hlm_buf_private*)BDBM_HLM_PRIV(bdi);
	DECLARE_WAITQUEUE (wait, current);
	struct bdbm_hlm_req_t* r;


	bdbm_daemonize ("hlm_buf_thread");
	allow_signal (SIGKILL); 

	bdbm_mutex_lock (&p->hlm_thread_done);

	for (;;) {
		if (bdbm_queue_is_all_empty (p->q)) {
			add_wait_queue (&p->hlm_wq, &wait);
			set_current_state (TASK_INTERRUPTIBLE);

			schedule ();

			remove_wait_queue (&p->hlm_wq, &wait);
			if (signal_pending (current))
				break; /* SIGKILL */
		}

		/* if nothing is in Q, then go to the next punit */
		while (!bdbm_queue_is_empty (p->q, 0)) {
			if ((r = (struct bdbm_hlm_req_t*)bdbm_queue_dequeue (p->q, 0)) != NULL) {
				/*bdbm_msg ("%llu: submit", r->lpa);*/
				if (hlm_nobuf_make_req (bdi, r)) {
					/* if it failed, we directly call 'ptr_host_inf->end_req' */
					bdi->ptr_host_inf->end_req (bdi, r);
					bdbm_warning ("oops! make_req failed");
					/* [CAUTION] r is now NULL */
				}
			} else {
				bdbm_error ("r == NULL");
				bdbm_bug_on (1);
			}
		}
	}

	bdbm_mutex_unlock (&p->hlm_thread_done);

	return 0;
}
#else
int __hlm_buf_thread (void* arg)
{
	struct bdbm_drv_info* bdi = (struct bdbm_drv_info*)arg;
	struct bdbm_hlm_buf_private* p = (struct bdbm_hlm_buf_private*)BDBM_HLM_PRIV(bdi);
	struct bdbm_hlm_req_t* r;

	for (;;) {
		if (bdbm_queue_is_all_empty (p->q)) {
			if (bdbm_thread_schedule (p->hlm_thread) == SIGKILL) {
				break;
			}
		}

		/* if nothing is in Q, then go to the next punit */
		while (!bdbm_queue_is_empty (p->q, 0)) {
			if ((r = (struct bdbm_hlm_req_t*)bdbm_queue_dequeue (p->q, 0)) != NULL) {
				/*bdbm_msg ("%llu: submit", r->lpa);*/
				if (hlm_nobuf_make_req (bdi, r)) {
					/* if it failed, we directly call 'ptr_host_inf->end_req' */
					bdi->ptr_host_inf->end_req (bdi, r);
					bdbm_warning ("oops! make_req failed");
					/* [CAUTION] r is now NULL */
				}
			} else {
				bdbm_error ("r == NULL");
				bdbm_bug_on (1);
			}
		}
	}

	return 0;
}
#endif

/* interface functions for hlm_buf */
uint32_t hlm_buf_create (struct bdbm_drv_info* bdi)
{
	struct bdbm_hlm_buf_private* p;

	/* create private */
	if ((p = (struct bdbm_hlm_buf_private*)bdbm_malloc_atomic
			(sizeof(struct bdbm_hlm_buf_private))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return 1;
	}

	/* setup FTL function pointers */
	if ((p->ptr_ftl_inf = BDBM_GET_FTL_INF (bdi)) == NULL) {
		bdbm_error ("ftl is not valid");
		return 1;
	}

	/* create a single queue */
	if ((p->q = bdbm_queue_create (1, INFINITE_QUEUE)) == NULL) {
		bdbm_error ("bdbm_queue_create failed");
		return -1;
	}

	/* keep the private structure */
	bdi->ptr_hlm_inf->ptr_private = (void*)p;

	/* create & run a thread */
#ifdef USE_ORIGINAL
	bdbm_mutex_init (&p->hlm_thread_done);

	init_waitqueue_head (&p->hlm_wq);
	if ((p->hlm_thread = kthread_create (
			__hlm_buf_thread, bdi, "__hlm_buf_thread")) == NULL) {
		bdbm_error ("kthread_create failed");
		return -1;
	}
	wake_up_process (p->hlm_thread);
#else
	if ((p->hlm_thread = bdbm_thread_create (
			__hlm_buf_thread, bdi, "__hlm_buf_thread")) == NULL) {
		bdbm_error ("kthread_create failed");
		return -1;
	}
#endif

	return 0;
}

void hlm_buf_destroy (struct bdbm_drv_info* bdi)
{
	struct bdbm_hlm_buf_private* p = (struct bdbm_hlm_buf_private*)bdi->ptr_hlm_inf->ptr_private;

	/* wait until Q becomes empty */
	while (!bdbm_queue_is_all_empty (p->q)) {
		msleep (1);
	}

	/* kill kthread */
#ifdef USE_ORIGINAL
	send_sig (SIGKILL, p->hlm_thread, 0);
	bdbm_mutex_lock (&p->hlm_thread_done);
#else
	bdbm_thread_stop (p->hlm_thread);
#endif

	/* destroy queue */
	bdbm_queue_destroy (p->q);

	/* free priv */
	bdbm_free_atomic (p);
}

uint32_t hlm_buf_make_req (
	struct bdbm_drv_info* bdi, 
	struct bdbm_hlm_req_t* ptr_hlm_req)
{
	uint32_t ret;
	struct bdbm_hlm_buf_private* p = (struct bdbm_hlm_buf_private*)BDBM_HLM_PRIV(bdi);

	if (bdbm_queue_is_full (p->q)) {
		/* FIXME: wait unti queue has a enough room */
		bdbm_error ("it should not be happened!");
		bdbm_bug_on (1);
	} 
	
	/* put a request into Q */
	if ((ret = bdbm_queue_enqueue (p->q, 0, (void*)ptr_hlm_req))) {
		bdbm_msg ("bdbm_queue_enqueue failed");
	}

	/* wake up thread if it sleeps */
#ifdef USE_ORIGINAL
	wake_up_interruptible (&p->hlm_wq); 
#else
	bdbm_thread_wakeup (p->hlm_thread);
#endif

	return ret;
}

void hlm_buf_end_req (
	struct bdbm_drv_info* bdi, 
	struct bdbm_llm_req_t* ptr_llm_req)
{
	hlm_nobuf_end_req (bdi, ptr_llm_req);
}

