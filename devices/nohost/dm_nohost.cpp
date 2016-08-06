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

#elif defined (USER_MODE)
#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "debug.h"
#include "dm_nohost.h"
#include "dev_params.h"

#include "utime.h"
#include "umemory.h"


/* interface for dm */
bdbm_dm_inf_t _bdbm_dm_inf = {
	.ptr_private = NULL,
	.probe = dm_nohost_probe,
	.open = dm_nohost_open,
	.close = dm_nohost_close,
	.make_req = dm_nohost_make_req,
	.make_reqs = NULL,
	.end_req = dm_nohost_end_req,
	.load = NULL,
	.store = NULL,
};

/* private data structure for dm */
typedef struct {
	bdbm_spinlock_t lock;
	bdbm_llm_req_t** llm_reqs;
} dm_ramssd_private_t;

/* global data structure */
extern bdbm_drv_info_t* _bdi_dm;

//class FlashIndication: public FlashIndicationWrapper {
//public 
//FlashIndication(unsigned int id) : FlashIndicationWrapper(id) { }
//};
//
//asdfaf

uint32_t dm_nohost_probe (
	bdbm_drv_info_t* bdi, 
	bdbm_device_params_t* params)
{
	dm_ramssd_private_t* p = NULL;

	/* setup NAND parameters according to users' inputs */
	*params = get_default_device_params ();

	/* create a private structure for ramdrive */
	if ((p = (dm_ramssd_private_t*)bdbm_malloc
			(sizeof (dm_ramssd_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		goto fail;
	}

	/* OK! keep private info */
	bdi->ptr_dm_inf->ptr_private = (void*)p;

	bdbm_msg ("[dm_nohost_probe] probe done!");

	return 0;

fail:
	return -1;
}

uint32_t dm_nohost_open (bdbm_drv_info_t* bdi)
{
	dm_ramssd_private_t * p = (dm_ramssd_private_t*)BDBM_DM_PRIV (bdi);

	bdbm_msg ("[dm_nohost_open] open done!");

	return 0;
}

void dm_nohost_close (bdbm_drv_info_t* bdi)
{
	dm_ramssd_private_t* p = (dm_ramssd_private_t*)BDBM_DM_PRIV (bdi);

	bdbm_msg ("[dm_nohost_close] closed!");

	bdbm_free_atomic (p);
}

uint32_t dm_nohost_make_req (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r)
{
	uint32_t punit_id, ret;
	dm_ramssd_private_t* priv = (dm_ramssd_private_t*)BDBM_DM_PRIV (bdi);

	if (r->req_type == REQTYPE_READ_DUMMY) {
		_bdi_dm->ptr_dm_inf->end_req (bdi, r);
		return 0;
	}

	/* check punit (= tags) */
	punit_id = r->phyaddr.punit_id;

	bdbm_spin_lock (&priv->lock);
	if (priv->llm_reqs[punit_id] != NULL) {
		bdbm_spin_unlock (&priv->lock);
		bdbm_error ("punit_id (%u) is busy...", punit_id);
		bdbm_bug_on (1);
	} else
		priv->llm_reqs[punit_id] = r;
	bdbm_spin_unlock (&priv->lock);

	/* submit reqs to the device */
	switch (r->req_type) {
	case REQTYPE_WRITE:
	case REQTYPE_RMW_WRITE:
	case REQTYPE_GC_WRITE:
	case REQTYPE_META_WRITE:
		/*
		__copy_bio_to_dma (bdi, r);
		FlashRequest_writePage (
			&priv->intarr[3], 
			r->phyaddr.channel_no, 
			r->phyaddr.chip_no, 
			r->phyaddr.block_no+BLKOFS, 
			r->phyaddr.page_no, 
			punit_id);
		*/
		break;

	case REQTYPE_READ:
	case REQTYPE_RMW_READ:
	case REQTYPE_GC_READ:
	case REQTYPE_META_READ:
		/*
		FlashRequest_readPage (
			&priv->intarr[3], 
			r->phyaddr.channel_no, 
			r->phyaddr.chip_no, 
			r->phyaddr.block_no+BLKOFS, 
			r->phyaddr.page_no, 
			punit_id);
		*/
		break;

	case REQTYPE_GC_ERASE:
		/*
		FlashRequest_eraseBlock (
			&priv->intarr[3], 
			r->phyaddr.channel_no, 
			r->phyaddr.chip_no, 
			r->phyaddr.block_no+BLKOFS, 
			punit_id);
		*/
		break;

	default:
		break;
	}

	return ret;
}

void dm_nohost_end_req (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r)
{
	bdbm_bug_on (r == NULL);

	bdi->ptr_llm_inf->end_req (bdi, r);
}
