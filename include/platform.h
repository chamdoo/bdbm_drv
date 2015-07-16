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

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/version.h>

/* memory handling */
#define bdbm_malloc(a) vmalloc(a)
#define bdbm_zmalloc(a) vzalloc(a)
/*#define bdbm_malloc(a) kzalloc(a, GFP_ATOMIC)*/
/*#define bdbm_zmalloc(a) kzalloc(a, GFP_ATOMIC)*/
#define bdbm_free(a) do { vfree(a); a = NULL; } while (0)
/*#define bdbm_free(a) do { kfree(a); a = NULL; } while (0)*/
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

/* spinlock */
#define bdbm_spinlock_t spinlock_t
#define bdbm_spin_lock_init(a) spin_lock_init(a)
#define bdbm_spin_lock(a) spin_lock(a)
#define bdbm_spin_lock_irqsave(a,flag) spin_lock_irqsave(a,flag)
#define bdbm_spin_unlock(a) spin_unlock(a)
#define bdbm_spin_unlock_irqrestore(a,flag) spin_unlock_irqrestore(a,flag)

/* thread */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,5,0)
#define bdbm_daemonize(a) daemonize(a)
#else
#define bdbm_daemonize(a)
#endif

#else /* __KERNEL__ */

/* TODO: it is required to define platform-dependent functions using user-level functions */

#endif /* __KERNEL__ */

#endif /* _BLUEDBM_PLATFORM_H */ 
