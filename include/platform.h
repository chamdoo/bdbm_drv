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

#ifndef _BLUEDBM_PLATFORM_H
#define _BLUEDBM_PLATFORM_H

#if defined(KERNEL_MODE)

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>

/* memory handling */
#define bdbm_malloc(a) vmalloc(a)
#define bdbm_zmalloc(a) vzalloc(a)
#define bdbm_free(a) do { vfree(a); a = NULL; } while (0)
#define bdbm_malloc_atomic(a) kzalloc(a, GFP_ATOMIC)
#define bdbm_free_atomic(a) do { kfree(a); a = NULL; } while (0)
#define bdbm_memcpy(dst,src,size) memcpy(dst,src,size)
#define bdbm_memset(addr,val,size) memset(addr,val,size)

/* completion lock */
#define bdbm_completion	struct completion
#define	bdbm_init_completion(a)	init_completion(&a)
#define bdbm_wait_for_completion(a)	wait_for_completion(&a)
#define bdbm_try_wait_for_completion(a) try_wait_for_completion(&a)
#define bdbm_complete(a) complete(&a)
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,12,0)
#define bdbm_reinit_completion(a) INIT_COMPLETION(a)
#else
#define	bdbm_reinit_completion(a) reinit_completion(&a)
#endif

/* mutex */
#include <linux/mutex.h>
#define bdbm_mutex struct mutex
#define bdbm_mutex_init(a) mutex_init(a)
#define bdbm_mutex_lock(a) mutex_lock(a)
#define bdbm_mutex_lock_interruptible(a) mutex_lock_interruptible(a)
#define bdbm_mutex_unlock(a) mutex_unlock(a)
#define bdbm_mutex_try_lock(a) mutex_trylock(a)  /* 1: acquire 0: contention */
#define bdbm_mutex_free(a)

/* spinlock */
#define bdbm_spinlock_t spinlock_t
#define bdbm_spin_lock_init(a) spin_lock_init(a)
#define bdbm_spin_lock(a) spin_lock(a)
#define bdbm_spin_lock_irqsave(a,flag) spin_lock_irqsave(a,flag)
#define bdbm_spin_unlock(a) spin_unlock(a)
#define bdbm_spin_unlock_irqrestore(a,flag) spin_unlock_irqrestore(a,flag)
#define bdbm_spin_lock_destory(a)

/* thread */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,5,0)
#define bdbm_daemonize(a) daemonize(a)
#else
#define bdbm_daemonize(a)
#endif


#elif defined(USER_MODE) 

/* kernel-module-specific macros (make them do nothing) */
#define module_param(a,b,c)
#define MODULE_PARM_DESC(a,b)

/* spinlock */
#include <pthread.h>
#define bdbm_spinlock_t pthread_spinlock_t
#define bdbm_spin_lock_init(a) pthread_spin_init(a,0)
#define bdbm_spin_lock(a) pthread_spin_lock(a)
#define bdbm_spin_lock_irqsave(a,flag) pthread_spin_lock(a);
#define bdbm_spin_unlock(a) pthread_spin_unlock(a)
#define bdbm_spin_unlock_irqrestore(a,flag) pthread_spin_unlock(a)
#define bdbm_spin_lock_destory(a) pthread_spin_destroy(a)

#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"

/* memory handling */
#include <string.h>
#include <stdlib.h>
/*#define bdbm_malloc(a) malloc(a)*/
#define bdbm_malloc(a) calloc(a, 1)
#define bdbm_zmalloc(a) calloc(a, 1)	/* 1 byte by default */
#define bdbm_free(a) do { free(a); } while (0)
#define bdbm_malloc_atomic(a) calloc(a, 1) /* 1 byte by default */
#define bdbm_free_atomic(a) do { free(a); } while (0)
#define bdbm_memcpy(dst,src,size) memcpy(dst,src,size)
#define bdbm_memset(addr,val,size) memset(addr,val,size)

/* synchronization */
#define bdbm_mutex pthread_mutex_t 
#define bdbm_mutex_init(a) pthread_mutex_init(a, NULL)
#define bdbm_mutex_lock(a) pthread_mutex_lock(a)
#define bdbm_mutex_lock_interruptible(a) pthread_mutex_lock(a)
#define bdbm_mutex_unlock(a) pthread_mutex_unlock(a)
/* NOTE:
 * Linux kernel's mutex_trylock returns '1' when it accquires a lock,
 * while pthread's mutex_try_lock return '0' when it gets a lock.
 *
 * For a compatibility purpuse, bdbm_mutex_try_lock always returns '1' 
 * when a lock is accquired by a thread. Otherwise, it returns 0. */
#define bdbm_mutex_try_lock(a) ({ \
	int z = pthread_mutex_trylock(a); int ret; \
	if (z == 0) ret = 1; \
	else ret = -z; \
	ret; })
#define bdbm_mutex_free(a) pthread_mutex_destroy(a)


#else
/* ERROR CASE */
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif


#endif /* _BLUEDBM_PLATFORM_H */ 
