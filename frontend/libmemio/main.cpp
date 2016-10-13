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

#include <stdio.h>

#include "bdbm_drv.h"
#include "umemory.h" /* bdbm_malloc */
#include "devices.h" /* bdbm_dm_get_inf */
#include "debug.h" /* bdbm_msg */


static void __dm_intr_handler (bdbm_drv_info_t* bdi, bdbm_llm_req_t* r);

bdbm_llm_inf_t _bdbm_llm_inf = {
	.ptr_private = NULL,
	.create = NULL,
	.destroy = NULL,
	.make_req = NULL,
	.make_reqs = NULL,
	.flush = NULL,
	/* 'dm' automatically calls 'end_req' when it gets acks from devices */
	.end_req = __dm_intr_handler, 
};

typedef struct memio {
	bdbm_drv_info_t bdi;
	bdbm_llm_req_t* rr;
	int nr_punits;
} memio_t;



static void __dm_intr_handler (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r)
{
	/* it is called by an interrupt handler */
}

static int __memio_init_llm_reqs (memio_t* mio)
{
	int ret = 0;
	if ((mio->rr = (bdbm_llm_req_t*)bdbm_zmalloc (
			sizeof (bdbm_llm_req_t) * mio->nr_punits)) == NULL) {
		bdbm_error ("bdbm_zmalloc () failed");
		ret = -1;
	} else {
		int i = 0;
		for (i = 0; i < mio->nr_punits; i++) {
			mio->rr[i].done = (bdbm_sema_t*)bdbm_malloc (sizeof (bdbm_sema_t));
			bdbm_sema_init (mio->rr[i].done); /* start with unlock */
		}
	}
	return ret;
}

static memio_t* memio_open ()
{
	bdbm_drv_info_t* bdi = NULL;
	bdbm_dm_inf_t* dm = NULL;
	memio_t* mio = NULL;
	int ret;

	/* allocate a memio data structure */
	if ((mio = (memio_t*)bdbm_zmalloc (sizeof (memio_t))) == NULL) {
		bdbm_error ("bdbm_zmalloc() failed");
		return NULL;
	}
	bdi = &mio->bdi;

	/* initialize a device manager */
	if (bdbm_dm_init (bdi) != 0) {
		bdbm_error ("bdbm_dm_init() failed");
		goto fail;
	}

	/* get the device manager interface and assign it to bdi */
	if ((dm = bdbm_dm_get_inf (bdi)) == NULL) {
		bdbm_error ("bdbm_dm_get_inf() failed");
		goto fail;
	}
	bdi->ptr_dm_inf = dm;

	/* probe the device to see if it is working now */
	if ((ret = dm->probe (bdi, &bdi->parm_dev)) != 0) {
		bdbm_error ("dm->probe was NULL or probe() failed (%p, %d)", 
			dm->probe, ret);
		goto fail;
	}

	/* setup some internal values according to 
	 * device's organization */
	if (__memio_init_llm_reqs (mio) == -1) {
	}


	/* ok! open the device so that I/Os will be sent to it */
	if ((ret = dm->open (bdi)) != 0) {
		bdbm_error ("dm->open was NULL or open failed (%p. %d)", 
			dm->open, ret);
		goto fail;
	}

	return mio;

fail:
	if (mio)
		bdbm_free (mio);

	return NULL;
}

static int memio_write (memio_t* mio)
{
	return 0;
}

static int memio_read (memio_t* mio)
{
	return 0;
}

static int memio_trim (memio_t* mio)
{
	return 0;
}

static void memio_close (memio_t* mio)
{

}

int main (int argc, char** argv)
{
	memio_t* mio = NULL;
	
	if ((mio = memio_open ()) == NULL)
		goto fail;

	return 0;

fail:
	if (mio)
		memio_close (mio);

	return -1;
}

