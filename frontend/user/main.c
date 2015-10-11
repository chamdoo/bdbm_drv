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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/slab.h>

#elif defined(USER_MODE)
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#endif

#include "bdbm_drv.h"
#include "platform.h"
#include "params.h"
#include "ftl_params.h"
#include "debug.h"
#include "userio.h"
#include "ufile.h"

#include "llm_noq.h"
#include "llm_mq.h"
#include "hlm_nobuf.h"
#include "hlm_buf.h"
#include "hlm_dftl.h"
#include "hlm_rsd.h"
#include "devices.h"
#include "pmu.h"

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"
#include "algo/dftl.h"

/* main data structure */
bdbm_drv_info_t* _bdi = NULL;

#if 0
static int init_func_pointers (bdbm_drv_info_t* bdi)
{
	/* set functions for device manager (dm) */
	if (bdbm_dm_init (bdi) != 0)  {
		bdbm_error ("bdbm_dm_init failed");
		return 1;
	}
	bdi->ptr_dm_inf = bdbm_dm_get_inf (bdi);

	/* set functions for host */
	bdi->ptr_host_inf = &_host_user_inf;

	/* set functions for hlm */
	switch (bdi->parm_ftl.hlm_type) {
	case HLM_NO_BUFFER:
		bdi->ptr_hlm_inf = &_hlm_nobuf_inf;
		break;
	case HLM_BUFFER:
		bdi->ptr_hlm_inf = &_hlm_buf_inf;
		break;
	case HLM_DFTL:
		bdi->ptr_hlm_inf = &_hlm_dftl_inf;
		break;
	case HLM_RSD:
		bdi->ptr_hlm_inf = &_hlm_rsd_inf;
		break;
	default:
		bdbm_error ("invalid hlm type");
		bdbm_bug_on (1);
		break;
	}

	/* set functions for llm */
	switch (bdi->parm_ftl.llm_type) {
	case LLM_NO_QUEUE:
		bdi->ptr_llm_inf = &_llm_noq_inf;
		break;
	case LLM_MULTI_QUEUE:
		bdi->ptr_llm_inf = &_llm_mq_inf;
		break;
	default:
		bdbm_error ("invalid llm type");
		bdbm_bug_on (1);
		break;
	}

	/* set functions for ftl */
	switch (bdi->parm_ftl.mapping_type) {
	case MAPPING_POLICY_NO_FTL:
		bdi->ptr_ftl_inf = &_ftl_no_ftl;
		break;
	case MAPPING_POLICY_SEGMENT:
		bdi->ptr_ftl_inf = &_ftl_block_ftl;
		break;
	case MAPPING_POLICY_PAGE:
		bdi->ptr_ftl_inf = &_ftl_page_ftl;
		break;
	case MAPPING_POLICY_DFTL:
		bdi->ptr_ftl_inf = &_ftl_dftl;
		break;
	default:
		bdbm_error ("invalid ftl type");
		bdbm_bug_on (1);
		break;
	}

	return 0;
}

int bdbm_drv_init (void)
{
	bdbm_drv_info_t* bdi = NULL;
	bdbm_host_inf_t* host = NULL; 
	bdbm_dm_inf_t* dm = NULL;
	bdbm_hlm_inf_t* hlm = NULL;
	bdbm_llm_inf_t* llm = NULL;
	bdbm_ftl_inf_t* ftl = NULL;
	uint32_t load = 0;

	/* allocate the memory for bdbm_drv_info_t */
	if ((bdi = (bdbm_drv_info_t*)bdbm_malloc_atomic (sizeof (bdbm_drv_info_t))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail;
	}
	_bdi = bdi;

	/* get default driver paramters */
	bdi->parm_ftl = get_default_driver_params ();

	/* set function pointers */
	if (init_func_pointers (bdi) != 0) {
		bdbm_error ("failed to initialize function pointers");
		goto fail;
	}

	/* probe a device to get its geometry information */
	dm = bdi->ptr_dm_inf;
	if (dm->probe (bdi, &bdi->parm_dev) != 0) {
		bdbm_error ("failed to probe a flash device");
		goto fail;
	}
	/* open a flash device */
	if (dm->open (bdi) != 0) {
		bdbm_error ("failed to open a flash device");
		goto fail;
	}
	if (bdi->parm_ftl.snapshot == SNAPSHOT_ENABLE &&
		dm->load != NULL) {
		if (dm->load (bdi, "/usr/share/bdbm_drv/dm.dat") != 0) {
			bdbm_msg ("loading 'dm.dat' failed");
			load = 0;
		} else 
			load = 1;
	}

	/* create a low-level memory manager */
	llm = bdi->ptr_llm_inf;
	if (llm->create (bdi) != 0) {
		bdbm_error ("failed to create llm");
		goto fail;
	}

	/* create a logical-to-physical mapping manager */
	ftl = bdi->ptr_ftl_inf;
	if (ftl->create (bdi) != 0) {
		bdbm_error ("failed to create ftl");
		goto fail;
	}
	if (bdi->parm_ftl.snapshot == SNAPSHOT_ENABLE &&
		load == 1 && ftl->load != NULL) {
		if (ftl->load (bdi, "/usr/share/bdbm_drv/ftl.dat") != 0) {
			bdbm_msg ("loading 'ftl.dat' failed");
			/*goto fail;*/
		}
	}

	/* create a high-level memory manager */
	hlm = bdi->ptr_hlm_inf;
	if (hlm->create (bdi) != 0) {
		bdbm_error ("failed to create hlm");
		goto fail;
	}

	/* create a host interface */
	host = bdi->ptr_host_inf;
	if (host->open (bdi) != 0) {
		bdbm_error ("failed to open a host interface");
		goto fail;
	}

	/* display default parameters */
	display_driver_params (&bdi->parm_ftl);

	/* init performance monitor */
	pmu_create (bdi);

	bdbm_msg ("[blueDBM is registered]");

	return 0;

fail:
	if (host != NULL)
		host->close (bdi);
	if (hlm != NULL)
		hlm->destroy (bdi);
	if (ftl != NULL)
		ftl->destroy (bdi);
	if (llm != NULL)
		llm->destroy (bdi);
	if (dm != NULL)
		dm->close (bdi);
	if (bdi != NULL)
		bdbm_free_atomic (bdi);

	return -1;
}

void bdbm_drv_exit(void)
{
	bdbm_ftl_params* dp = BDBM_GET_DRIVER_PARAMS (_bdi);

	if (_bdi == NULL)
		return;

	if (_bdi->ptr_host_inf != NULL)
		_bdi->ptr_host_inf->close (_bdi);

	if (_bdi->ptr_hlm_inf != NULL)
		_bdi->ptr_hlm_inf->destroy (_bdi);

	if (_bdi->ptr_ftl_inf != NULL)
		if (dp->snapshot == SNAPSHOT_ENABLE && _bdi->ptr_ftl_inf->store)
			_bdi->ptr_ftl_inf->store (_bdi, "/usr/share/bdbm_drv/ftl.dat");
		_bdi->ptr_ftl_inf->destroy (_bdi);

	if (_bdi->ptr_llm_inf != NULL)
		_bdi->ptr_llm_inf->destroy (_bdi);

	if (_bdi->ptr_dm_inf != NULL) {
		if (dp->snapshot == SNAPSHOT_ENABLE && _bdi->ptr_dm_inf->store)
			_bdi->ptr_dm_inf->store (_bdi, "/usr/share/bdbm_drv/dm.dat");
		_bdi->ptr_dm_inf->close (_bdi);
		bdbm_dm_exit (_bdi);
	}

	/* display performance results */
	pmu_display (_bdi);
	pmu_destory (_bdi);

	bdbm_free_atomic (_bdi);

	bdbm_msg ("[blueDBM is removed]");
}
#endif


#define NUM_THREADS	100
/*#define NUM_THREADS	20*/
/*#define NUM_THREADS	10*/
/*#define NUM_THREADS	1*/

#include "bdbm_drv.h"
#include "platform.h"
#include "uatomic64.h"

void host_thread_fn (void *data) 
{
	int loop;
	int lpa = 0;
	int unique_id = 0;
	int *val = (int*)(data);
	int step = 31;

	for (loop = 0; loop < 10000; loop++) {
		bdbm_host_req_t* host_req = NULL;

		host_req = (bdbm_host_req_t*)malloc (sizeof (bdbm_host_req_t));

		host_req->uniq_id = (*val);
		(*val)++;
		host_req->req_type = REQTYPE_WRITE;
		host_req->lpa = lpa;
		host_req->len = step;
		host_req->data = (uint8_t*)malloc (4096 * host_req->len);

		host_req->data[0] = 0xA;
		host_req->data[1] = 0xB;
		host_req->data[2] = 0xC;

		_bdi->ptr_host_inf->make_req (_bdi, host_req);

		lpa+=step;
	}

	pthread_exit (0);
}

void host_thread_fn2 (void *data) 
{
	int loop;
	int lpa = 0;
	int unique_id = 0;
	int *val = (int*)(data);
	int step = 31;

	for (loop = 0; loop < 10000; loop++) {
		bdbm_host_req_t* host_req = NULL;

		host_req = (bdbm_host_req_t*)malloc (sizeof (bdbm_host_req_t));

		host_req->uniq_id = (*val);
		/*host_req->uniq_id = ((uint64_t)host_req) % (2*1024*1024/4-32);*/
		(*val)++;
		host_req->req_type = REQTYPE_WRITE;
		/*host_req->lpa = lpa;*/
		host_req->lpa = ((uint64_t)host_req) % (2*1024*1024/4-32);
		/*bdbm_msg ("%llu", host_req->lpa);*/
		host_req->len = step;
		host_req->data = (uint8_t*)malloc (4096 * host_req->len);

		host_req->data[0] = 0xA;
		host_req->data[1] = 0xB;
		host_req->data[2] = 0xC;

		_bdi->ptr_host_inf->make_req (_bdi, host_req);

		lpa+=step;
	}

	pthread_exit (0);
}

#if 0
int main (int argc, char** argv)
{
	int loop_thread;

	pthread_t thread[NUM_THREADS];
	int thread_args[NUM_THREADS];

	bdbm_msg ("[main] run ftlib... (%d)", sizeof (bdbm_llm_req_t));

	bdbm_msg ("[main] initialize bdbm_drv");
	if (bdbm_drv_init () == -1) {
		bdbm_msg ("[main] initialization failed");
		return -1;
	}

	do {
		bdbm_msg ("[main] run some simulation");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			thread_args[loop_thread] = loop_thread;
			pthread_create (&thread[loop_thread], NULL, 
				(void*)&host_thread_fn2, 
				(void*)&thread_args[loop_thread]);
		}

		bdbm_msg ("[main] wait for threads to end...");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			pthread_join (
				thread[loop_thread], NULL
			);
		}

		bdbm_msg ("START READ");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			thread_args[loop_thread] = loop_thread;
			pthread_create (&thread[loop_thread], NULL, 
				(void*)&host_thread_fn, 
				(void*)&thread_args[loop_thread]);
		}

		bdbm_msg ("[main] wait for threads to end...");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			pthread_join (
				thread[loop_thread], NULL
			);
		}

	} while (1);

	bdbm_msg ("[main] destroy bdbm_drv");
	bdbm_drv_exit ();

	bdbm_msg ("[main] done");

	return 0;
}
#endif

int main (int argc, char** argv)
{
	int loop_thread;

	pthread_t thread[NUM_THREADS];
	int thread_args[NUM_THREADS];

	bdbm_msg ("[main] run ftlib... (%d)", sizeof (bdbm_llm_req_t));

	bdbm_msg ("[user-main] initialize bdbm_drv");
	if ((_bdi = bdbm_drv_create ()) == NULL) {
		bdbm_error ("[kmain] bdbm_drv_create () failed");
		return -1;
	}

	if (bdbm_dm_init (_bdi) != 0) {
		bdbm_error ("[kmain] bdbm_dm_init () failed");
		return -1;
	}

	bdbm_drv_setup (_bdi, &_host_user_inf, bdbm_dm_get_inf (_bdi));
	bdbm_drv_run (_bdi);

	do {
		bdbm_msg ("[main] run some simulation");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			thread_args[loop_thread] = loop_thread;
			pthread_create (&thread[loop_thread], NULL, 
				(void*)&host_thread_fn2, 
				(void*)&thread_args[loop_thread]);
		}

		bdbm_msg ("[main] wait for threads to end...");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			pthread_join (
				thread[loop_thread], NULL
			);
		}

		bdbm_msg ("START READ");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			thread_args[loop_thread] = loop_thread;
			pthread_create (&thread[loop_thread], NULL, 
				(void*)&host_thread_fn, 
				(void*)&thread_args[loop_thread]);
		}

		bdbm_msg ("[main] wait for threads to end...");
		for (loop_thread = 0; loop_thread < NUM_THREADS; loop_thread++) {
			pthread_join (
				thread[loop_thread], NULL
			);
		}

	} while (1);

	bdbm_msg ("[main] destroy bdbm_drv");
	bdbm_drv_close (_bdi);
	bdbm_dm_exit (_bdi);
	bdbm_drv_destroy (_bdi);

	bdbm_msg ("[main] done");

	return 0;
}

