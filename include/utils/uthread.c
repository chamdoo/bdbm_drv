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

#include "debug.h"
#include "platform.h"
#include "uthread.h"


/**************************************************************************/
#if defined(KERNEL_MODE)
#include <linux/init.h>
#include <linux/module.h>

int bdbm_thread_fn (void *data) 
{
	bdbm_thread_t* k = (bdbm_thread_t*)data;

	/* initialize a kernel thread */
	DECLARE_WAITQUEUE (wait, current);
	k->wait = &wait;

	bdbm_daemonize ("bdbm_thread_fn");
	allow_signal (SIGKILL); 

	bdbm_mutex_lock (&k->thread_done);
	
	/* invoke a user call-back function */
	k->user_threadfn (k->user_data);


	/* a thread stops */
	k->wait = NULL;
	bdbm_mutex_unlock (&k->thread_done);

	return 0;
};

bdbm_thread_t* bdbm_thread_create (
	int (*user_threadfn)(void *data), 
	void* user_data, 
	char* name)
{
	bdbm_thread_t* k = NULL;

	/* create bdbm_thread_t */
	if ((k = (bdbm_thread_t*)bdbm_malloc_atomic (
			sizeof (bdbm_thread_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		return NULL;
	}

	/* initialize bdbm_thread_t */
	k->user_threadfn = user_threadfn;
	k->user_data = (void*)user_data;
	k->wait = NULL;
	bdbm_mutex_init (&k->thread_done);
	init_waitqueue_head (&k->wq);
	if ((k->thread = kthread_create (
			bdbm_thread_fn, (void*)k, name)) == NULL) {
		bdbm_error ("kthread_create failed");
		bdbm_free_atomic (k);
		return NULL;
	} 

	/* wake up thread! */
	wake_up_process (k->thread);

	return k;
}

int bdbm_thread_schedule (bdbm_thread_t* k)
{
	if (k == NULL || k->wait == NULL) {
		bdbm_error ("oops! k or k->wait is NULL (%p, %p)", k, k->wait);
		return 0;
	}

	add_wait_queue (&k->wq, k->wait);
	set_current_state (TASK_INTERRUPTIBLE);
	schedule (); /* go to sleep */
	remove_wait_queue (&k->wq, k->wait);

	if (signal_pending (current)) {
		/* get a kill signal */
		return SIGKILL;
	}

	return 0;
}

void bdbm_thread_wakeup (bdbm_thread_t* k)
{
	if (k == NULL) {
		bdbm_error ("oops! k is NULL");
		return;
	}

	wake_up_interruptible (&k->wq);
}

void bdbm_thread_stop (bdbm_thread_t* k)
{
	if (k == NULL) {
		bdbm_error ("oops! k is NULL");
		return;
	}

	/* send a KILL signal to the thread */
	send_sig (SIGKILL, k->thread, 0);
	bdbm_mutex_lock (&k->thread_done);

	/* free bdbm_thread_t */
	bdbm_free_atomic (k);
}

void bdbm_thread_msleep (uint32_t ms) 
{
	msleep (ms);
}

void bdbm_thread_yield ()
{
	yield ();
}

#endif /* KERNEL_MODE */


/**************************************************************************/
#if defined(USER_MODE)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/time.h>

#include <inttypes.h>
#include <pthread.h>

int bdbm_thread_fn (void *data) 
{
	return 0;
};

bdbm_thread_t* bdbm_thread_create (
	int (*user_threadfn)(void *data), 
	void* user_data, 
	char* name)
{
	return NULL;
}

int bdbm_thread_schedule (bdbm_thread_t* k)
{
	return 0;
}

void bdbm_thread_wakeup (bdbm_thread_t* k)
{
}

void bdbm_thread_stop (bdbm_thread_t* k)
{
}

void bdbm_thread_msleep (uint32_t ms) 
{
	int microsecs;
	struct timeval tv;
	microsecs = ms * 1000;
	tv.tv_sec  = microsecs / 1000000;
	tv.tv_usec = microsecs % 1000000;
	select (0, NULL, NULL, NULL, &tv);  
}

void bdbm_thread_yield (void)
{
	pthread_yield ();
}



#endif

