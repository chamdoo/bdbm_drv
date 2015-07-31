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
#include "uparams.h"
#include "debug.h"
#include "host_user.h"

#include "llm_noq.h"
#include "llm_mq.h"
#include "hlm_nobuf.h"
#include "hlm_buf.h"
#include "hlm_rsd.h"
#include "dm_user.h"
#include "hw.h"

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"
#include "utils/ufile.h"

/* main data structure */
struct bdbm_drv_info* _bdi = NULL;

static int init_func_pointers (struct bdbm_drv_info* bdi)
{
	struct bdbm_params* p = bdi->ptr_bdbm_params;

	/* set functions for device manager (dm) */
	bdi->ptr_dm_inf = &_dm_user_inf;

	/* set functions for host */
	bdi->ptr_host_inf = &_host_user_inf;

	/* set functions for hlm */
	switch (p->driver.hlm_type) {
	case HLM_NO_BUFFER:
		bdi->ptr_hlm_inf = &_hlm_nobuf_inf;
		break;
	case HLM_BUFFER:
		bdi->ptr_hlm_inf = &_hlm_buf_inf;
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
	switch (p->driver.llm_type) {
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
	switch (p->driver.mapping_type) {
	case MAPPING_POLICY_NO_FTL:
		bdi->ptr_ftl_inf = &_ftl_no_ftl;
		break;
	case MAPPING_POLICY_SEGMENT:
		bdi->ptr_ftl_inf = &_ftl_block_ftl;
		break;
	case MAPPING_POLICY_PAGE:
		bdi->ptr_ftl_inf = &_ftl_page_ftl;
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
	struct bdbm_drv_info* bdi = NULL;
	struct bdbm_host_inf_t* host = NULL; 
	struct bdbm_dm_inf_t* dm = NULL;
	struct bdbm_hlm_inf_t* hlm = NULL;
	struct bdbm_llm_inf_t* llm = NULL;
	struct bdbm_ftl_inf_t* ftl = NULL;
	/*struct bdbm_params* ptr_params = NULL;*/
#ifdef SNAPSHOT_ENABLE
	uint32_t load = 0;
#endif

	/* allocate the memory for bdbm_drv_info */
	if ((bdi = (struct bdbm_drv_info*)bdbm_malloc_atomic (sizeof (struct bdbm_drv_info))) == NULL) {
		bdbm_error ("bdbm_malloc_atomic failed");
		goto fail;
	}
	_bdi = bdi;

	/* get default driver paramters */
	if ((bdi->ptr_bdbm_params = read_driver_params ()) == NULL) {
		bdbm_error ("failed to read the default parameters");
		goto fail;
	}

	/* set function pointers */
	if (init_func_pointers (bdi) != 0) {
		bdbm_error ("failed to initialize function pointers");
		goto fail;
	}

	/* probe a device to get its geometry information */
	dm = bdi->ptr_dm_inf;
	if (dm->probe (bdi, &bdi->ptr_bdbm_params->nand) != 0) {
		bdbm_error ("failed to probe a flash device");
		goto fail;
	}
	/* open a flash device */
	if (dm->open (bdi) != 0) {
		bdbm_error ("failed to open a flash device");
		goto fail;
	}
#ifdef SNAPSHOT_ENABLE
	if (dm->load != NULL) {
		if (dm->load (bdi, "/usr/share/bdbm_drv/dm.dat") != 0) {
			bdbm_msg ("loading 'dm.dat' failed");
			load = 0;
		} else 
			load = 1;
	}
#endif

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
#ifdef SNAPSHOT_ENABLE
	if (load == 1) {
		if (ftl->load != NULL) {
			if (ftl->load (bdi, "/usr/share/bdbm_drv/ftl.dat") != 0) {
				bdbm_msg ("loading 'ftl.dat' failed");
				/*goto fail;*/
			}
		} else {
			/*goto fail;*/
		}
	}
#endif

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
	display_default_params (bdi);

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
	if (_bdi == NULL)
		return;

	/* display performance results */
	pmu_display (_bdi);
	pmu_destory (_bdi);

	if (_bdi->ptr_host_inf != NULL)
		_bdi->ptr_host_inf->close (_bdi);

	if (_bdi->ptr_hlm_inf != NULL)
		_bdi->ptr_hlm_inf->destroy (_bdi);

	if (_bdi->ptr_ftl_inf != NULL)
#ifdef SNAPSHOT_ENABLE
		if (_bdi->ptr_ftl_inf->store)
			_bdi->ptr_ftl_inf->store (_bdi, "/usr/share/bdbm_drv/ftl.dat");
#endif
		_bdi->ptr_ftl_inf->destroy (_bdi);

	if (_bdi->ptr_llm_inf != NULL)
		_bdi->ptr_llm_inf->destroy (_bdi);

	if (_bdi->ptr_dm_inf != NULL) {
#ifdef SNAPSHOT_ENABLE
		if (_bdi->ptr_dm_inf->store)
			_bdi->ptr_dm_inf->store (_bdi, "/usr/share/bdbm_drv/dm.dat");
#endif
		_bdi->ptr_dm_inf->close (_bdi);
	}

	bdbm_free_atomic (_bdi);

	bdbm_msg ("[blueDBM is removed]");
}

int main (int argc, char** argv)
{
	int loop;
	struct bdbm_host_req_t host_req;

	bdbm_msg ("run ftlib...");

	bdbm_msg ("initialize bdbm_drv");
	if (bdbm_drv_init () == -1) {
		bdbm_msg ("initialization failed");
		return -1;
	}

	bdbm_msg ("run some simulation");

	host_req.req_type = REQTYPE_READ;
	host_req.lpa = 0;
	host_req.len = 4;
	host_req.data = (uint8_t*)malloc(4096*host_req.len);

	for (loop = 0; loop < 1000; loop++) {
		_bdi->ptr_host_inf->make_req (_bdi, &host_req);
		host_req.lpa++;
	}

	bdbm_msg ("destroy bdbm_drv");
	bdbm_drv_exit ();

	return 0;
}

