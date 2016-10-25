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
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

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
	/* 'dm' calls 'end_req' automatically
	 * when it gets acks from devices */
	.end_req = __dm_intr_handler, 
};

typedef struct memio {
	bdbm_drv_info_t bdi;
	bdbm_llm_req_t* rr;
	int nr_punits;
	size_t io_size; /* bytes */
	size_t trim_size;
	size_t trim_lbas;
} memio_t;

static bdbm_llm_req_t* __memio_alloc_llm_req (memio_t* mio);
static void __memio_free_llm_req (memio_t* mio, bdbm_llm_req_t* r);

static void __dm_intr_handler (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r)
{
	/* it is called by an interrupt handler */
	__memio_free_llm_req ((memio_t*)bdi->private_data, r);
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
	mio->nr_punits = 64; /* FIXME: it must be set according to device parameters */
	mio->io_size = 8192;
	mio->trim_lbas = (1 << 14);
	mio->trim_size = mio->trim_lbas * mio->io_size;

	/* setup some internal values according to 
	 * the device's organization */
	if ((ret = __memio_init_llm_reqs (mio)) != 0) {
		bdbm_error ("__memio_init_llm_reqs () failed (%d)", 
			ret);
		goto fail;
	}

	/* setup function points; this is just to handle responses from the device */
	bdi->ptr_llm_inf = &_bdbm_llm_inf;

	/* assign rf to bdi's private_data */
	bdi->private_data = (void*)mio;

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

static bdbm_llm_req_t* __memio_alloc_llm_req (memio_t* mio)
{
	int i = 0;
	bdbm_llm_req_t* r = NULL;

	/* get available llm_req */
	do {
		for (i = 0; i < mio->nr_punits; i++) { /* <= FIXME: use the linked-list instead of loop! */
			if (!bdbm_sema_try_lock (mio->rr[i].done))
				continue;
			r = (bdbm_llm_req_t*)&mio->rr[i];
			r->tag = i;
			break;
		}
	} while (!r); /* <= FIXME: use the event instead of loop! */

	return r;
}

static void __memio_free_llm_req (memio_t* mio, bdbm_llm_req_t* r)
{
	/* release semaphore */
	r->tag = -1;
	bdbm_sema_unlock (r->done);
}

static void __memio_check_alignment (size_t length, size_t alignment)
{
	if ((length % alignment) != 0) {
		bdbm_error ("alignment error occurs (length = %d, alignment = %d)",
			length, alignment);
		exit (-1);
	}
}

static int __memio_do_io (memio_t* mio, int dir, size_t lba, size_t len, uint8_t* data)
{
	bdbm_llm_req_t* r = NULL;
	bdbm_dm_inf_t* dm = mio->bdi.ptr_dm_inf;
	uint8_t* cur_buf = data;
	size_t cur_lba = lba;
	size_t sent = 0;
	int ret;
	
	/* see if LBA alignment is correct */
	__memio_check_alignment (len, mio->io_size);

	/* fill up logaddr; note that phyaddr is not used here */
	while (cur_lba < lba + (len/mio->io_size)) {
		/* get an empty llm_req */
		r = __memio_alloc_llm_req (mio);

		bdbm_bug_on (!r);

		/* setup llm_req */
		r->req_type = (dir == 0) ? REQTYPE_READ : REQTYPE_WRITE;
		r->logaddr.lpa[0] = cur_lba;
		r->fmain.kp_ptr[0] = cur_buf;

		/* send I/O requets to the device */
		if ((ret = dm->make_req (&mio->bdi, r)) != 0) {
			bdbm_error ("dm->make_req() failed (ret = %d)", ret);
			bdbm_bug_on (1);
		}

		/* go the next */
		cur_lba += 1;
		cur_buf += mio->io_size;
		sent += mio->io_size;
	}

	/* return the length of bytes transferred */
	return sent;
}

static void memio_wait (memio_t* mio)
{
	int i;
	for (i = 0; i < mio->nr_punits; ) {
		if (!bdbm_sema_try_lock (mio->rr[i].done))
			continue;
		bdbm_sema_unlock (mio->rr[i].done);
		i++;
	}
}

static int memio_read (memio_t* mio, size_t lba, size_t len, uint8_t* data)
{
	return __memio_do_io (mio, 0, lba, len, data);
}

static int memio_write (memio_t* mio, size_t lba, size_t len, uint8_t* data)
{
	return __memio_do_io (mio, 1, lba, len, data);
}

static int memio_trim (memio_t* mio, size_t lba, size_t len)
{
	bdbm_llm_req_t* r = NULL;
	bdbm_dm_inf_t* dm = mio->bdi.ptr_dm_inf;
	size_t cur_lba = lba;
	size_t sent = 0;
	int ret, i;

	/* see if LBA alignment is correct */
	__memio_check_alignment (lba, mio->trim_lbas);
	__memio_check_alignment (len, mio->trim_size);

	/* fill up logaddr; note that phyaddr is not used here */
	while (cur_lba < lba + (len/mio->io_size)) {
		bdbm_msg ("segment #: %d", cur_lba / mio->trim_lbas);
		for (i = 0; i < mio->nr_punits; i++) {
			/* get an empty llm_req */
			r = __memio_alloc_llm_req (mio);

			bdbm_bug_on (!r);

			/* setup llm_req */
			//bdbm_msg ("  -- blk #: %d", i);
			r->req_type = REQTYPE_GC_ERASE;
			r->logaddr.lpa[0] = cur_lba + i;
			r->fmain.kp_ptr[0] = NULL;	/* no data; it must be NULL */

			/* send I/O requets to the device */
			if ((ret = dm->make_req (&mio->bdi, r)) != 0) {
				bdbm_error ("dm->make_req() failed (ret = %d)", ret);
				bdbm_bug_on (1);
			}
		}

		/* go the next */
		cur_lba += mio->trim_lbas;
		sent += mio->trim_size;
	}

	/* return the length of bytes transferred */
	return sent;
}

static void memio_close (memio_t* mio)
{
	bdbm_drv_info_t* bdi = NULL;
	bdbm_dm_inf_t* dm = NULL;
	int i;

	/* mio is available? */
	if (!mio) return;

	/* get pointers for dm and bdi */
	bdi = &mio->bdi;
	dm = bdi->ptr_dm_inf;

	/* wait for all the on-going jobs to finish */
	bdbm_msg ("Wait for all the on-going jobs to finish...");
	if (mio->rr) {
		for (i = 0; i < mio->nr_punits; i++)
			if (mio->rr[i].done)
				bdbm_sema_lock (mio->rr[i].done);
	}

	/* close the device interface */
	bdi->ptr_dm_inf->close (bdi);

	/* close the device module */
	bdbm_dm_exit (&mio->bdi);

	/* free allocated memory */
	if (mio->rr) {
		for (i = 0; i < mio->nr_punits; i++)
			if (mio->rr[i].done)
				bdbm_free (mio->rr[i].done);
		bdbm_free (mio->rr);
	}
	bdbm_free (mio);
}

int main (int argc, char** argv)
{
	int i = 0, j = 0, tmplba = 0;
	srand(time(NULL));

	memio_t* mio = NULL;
	int32_t wdata[2048], rdata[2048]; // uint8_t -> int32_t
	
	/* open the device */
	if ((mio = memio_open ()) == NULL)
		goto fail;

	/* perform some operations = test only 512 segments */
	for (i = 0; i < 512; i++) {
		memio_trim (mio, i<<14, (1<<14)*8192);
		memio_wait (mio);
	}

	for (i = 0; i < 512*(1<<14); i++) {
		/* 4 bytes of LSB and MSB are LBA */
		wdata[0] = i;    
		wdata[2047] = i;

		memio_write (mio, i, 8192, (uint8_t*)wdata);

		if (i%100 == 0) {
			printf ("w");
			fflush (stdout);
		}
	}
	memio_wait (mio);

	/* test random LBAs and read */
	printf ("start reads...\n\n\n");
	for (i = 0; i < 64*(1<<14); i++) {   // reducing the test case
		tmplba = rand() % (512*(1<<14)); // try random lba

		memio_read (mio, tmplba, 8192, (uint8_t*)rdata);
		memio_wait (mio);
		if (rdata[0] != tmplba || rdata[2047] != tmplba) {
			bdbm_msg ("[%d] OOPS! LBA=%d rdata[0] = %d, rdata[2047] = %d", i, tmplba, rdata[0], rdata[8191]);
		}
		if (i%100 == 0) {
			printf ("r");
			fflush (stdout);
		}
	}

	/* close the device */
	memio_close (mio);
	return 0;

fail:
	return -1;
}
