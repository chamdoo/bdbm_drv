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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/slab.h>

#include "bdbm_drv.h"
#include "platform.h"
#include "params.h"
#include "kparams.h"
#include "debug.h"
#include "host_blockio.h"

#include "llm_noq.h"
#include "llm_mq.h"
#include "hlm_nobuf.h"
#include "hlm_buf.h"
#include "hlm_dftl.h"
#include "hlm_rsd.h"
#include "hw.h"
#include "pmu.h"

#include "algo/no_ftl.h"
#include "algo/block_ftl.h"
#include "algo/page_ftl.h"
#include "algo/dftl.h"
#include "utils/ufile.h"

#if defined (USE_BLOCKIO_PROXY)
#include "host_blockio_proxy.h"
#endif

/* main data structure */
bdbm_drv_info_t* _bdi = NULL;

static int init_func_pointers (bdbm_drv_info_t* bdi)
{
	bdbm_params_t* p = bdi->ptr_bdbm_params;

	/* set functions for device manager (dm) */
#if !defined (USE_BLOCKIO_PROXY)
	if (bdbm_dm_init (bdi) != 0)  {
		bdbm_error ("bdbm_dm_init failed");
		return 1;
	}
	bdi->ptr_dm_inf = bdbm_dm_get_inf (bdi);
#else
	bdi->ptr_dm_inf = NULL;
#endif

	/* set functions for host */
	switch (p->driver.host_type) {
	case HOST_NOT_SPECIFIED:
		bdi->ptr_host_inf = NULL;
		break;
	case HOST_BLOCK:
		bdi->ptr_host_inf = &_host_block_inf;
		break;
	case HOST_PROXY:
		/* TODO */
	case HOST_DIRECT:
	default:
		bdbm_error ("invalid host type");
		bdbm_bug_on (1);
		break;
	}

	/* set functions for hlm */
	switch (p->driver.hlm_type) {
	case HLM_NOT_SPECIFIED:
		bdi->ptr_hlm_inf = NULL;
		break;
	case HLM_NO_BUFFER:
		bdi->ptr_hlm_inf = &_hlm_nobuf_inf;
		break;
	case HLM_BUFFER:
		bdi->ptr_hlm_inf = &_hlm_buf_inf;
		break;
	case HLM_RSD:
		bdi->ptr_hlm_inf = &_hlm_rsd_inf;
		break;
	case HLM_DFTL:
		bdi->ptr_hlm_inf = &_hlm_dftl_inf;
		break;
	default:
		bdbm_error ("invalid hlm type");
		bdbm_bug_on (1);
		break;
	}

	/* set functions for llm */
	switch (p->driver.llm_type) {
	case LLM_NOT_SPECIFIED:
		bdi->ptr_llm_inf = NULL;
		break;
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
	case MAPPING_POLICY_NOT_SPECIFIED:
		bdi->ptr_ftl_inf = NULL;
		break;
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

static int __init bdbm_drv_init (void)
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
	if (bdi->ptr_dm_inf) {
		dm = bdi->ptr_dm_inf;

		/* get the device information */
		if (dm->probe (bdi, &bdi->ptr_bdbm_params->nand) != 0) {
			bdbm_error ("failed to probe a flash device");
			goto fail;
		}
		/* open a flash device */
		if (dm->open (bdi) != 0) {
			bdbm_error ("failed to open a flash device");
			goto fail;
		}
		/* do we need to read a snapshot? */
		if (bdi->ptr_bdbm_params->driver.snapshot == SNAPSHOT_ENABLE &&
			dm->load != NULL) {
			if (dm->load (bdi, "/usr/share/bdbm_drv/dm.dat") != 0) {
				bdbm_msg ("loading 'dm.dat' failed");
				load = 0;
			} else 
				load = 1;
		}
	}

	/* create a low-level memory manager */
	if (bdi->ptr_llm_inf) {
		llm = bdi->ptr_llm_inf;
		if (llm->create (bdi) != 0) {
			bdbm_error ("failed to create llm");
			goto fail;
		}
	}

	/* create a logical-to-physical mapping manager */
	if (bdi->ptr_ftl_inf) {
		ftl = bdi->ptr_ftl_inf;
		if (ftl->create (bdi) != 0) {
			bdbm_error ("failed to create ftl");
			goto fail;
		}
		if (bdi->ptr_bdbm_params->driver.snapshot == SNAPSHOT_ENABLE &&
			load == 1 && ftl->load != NULL) {
			if (ftl->load (bdi, "/usr/share/bdbm_drv/ftl.dat") != 0) {
				bdbm_msg ("loading 'ftl.dat' failed");
			}
		}
	}

	/* create a high-level memory manager */
	if (bdi->ptr_hlm_inf) {
		hlm = bdi->ptr_hlm_inf;
		if (hlm->create (bdi) != 0) {
			bdbm_error ("failed to create hlm");
			goto fail;
		}
	}

	/* create a host interface */
	if (bdi->ptr_host_inf) {
		host = bdi->ptr_host_inf;
		if (host->open (bdi) != 0) {
			bdbm_error ("failed to open a host interface");
			goto fail;
		}
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

	return -ENXIO;
}

static void __exit bdbm_drv_exit(void)
{
	driver_params_t* dp = BDBM_GET_DRIVER_PARAMS (_bdi);
	bdbm_drv_info_t* bdi = _bdi;

	if (bdi == NULL)
		return;

	/* display performance results */
	pmu_display (bdi);
	pmu_destory (bdi);

	if (bdi->ptr_host_inf != NULL)
		bdi->ptr_host_inf->close (bdi);

	if (bdi->ptr_hlm_inf != NULL)
		bdi->ptr_hlm_inf->destroy (bdi);

	if (bdi->ptr_ftl_inf != NULL)
		if (dp->snapshot == SNAPSHOT_ENABLE && bdi->ptr_ftl_inf->store)
			bdi->ptr_ftl_inf->store (bdi, "/usr/share/bdbm_drv/ftl.dat");
		bdi->ptr_ftl_inf->destroy (bdi);

	if (bdi->ptr_llm_inf != NULL)
		bdi->ptr_llm_inf->destroy (bdi);

	if (bdi->ptr_dm_inf != NULL) {
		if (dp->snapshot == SNAPSHOT_ENABLE && bdi->ptr_dm_inf->store)
			bdi->ptr_dm_inf->store (bdi, "/usr/share/bdbm_drv/dm.dat");
		bdi->ptr_dm_inf->close (bdi);
#if !defined (USE_BLOCKIO_PROXY)
		bdbm_dm_exit (bdi);
#endif
	}

	bdbm_free_atomic (bdi);

	bdbm_msg ("[blueDBM is removed]");
}

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("BlueDBM Device Driver");
MODULE_LICENSE ("GPL");

module_init (bdbm_drv_init);
module_exit (bdbm_drv_exit);
