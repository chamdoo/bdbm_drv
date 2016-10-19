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


/***/
#include "FlashIndication.h"
#include "FlashRequest.h"
#include "dmaManager.h"

#define BLOCKS_PER_CHIP 32
#define CHIPS_PER_BUS 8 // 8
#define NUM_BUSES 8 // 8

#define FPAGE_SIZE (8192*2)
#define FPAGE_SIZE_VALID (8224)
#define NUM_TAGS 128

#define BLOCKS_PER_CHIP 32
#define CHIPS_PER_BUS 8 // 8
#define NUM_BUSES 8 // 8

#define FPAGE_SIZE (8192*2)
#define FPAGE_SIZE_VALID (8224)
#define NUM_TAGS 128

typedef enum {
	UNINIT,
	ERASED,
	WRITTEN
} FlashStatusT;

typedef struct {
	bool busy;
	int bus;
	int chip;
	int block;
} TagTableEntry;

FlashRequestProxy *device;

pthread_mutex_t flashReqMutex;
pthread_cond_t flashFreeTagCond;

//8k * 128
size_t dstAlloc_sz = FPAGE_SIZE * NUM_TAGS *sizeof(unsigned char);
size_t srcAlloc_sz = FPAGE_SIZE * NUM_TAGS *sizeof(unsigned char);
int dstAlloc;
int srcAlloc;
unsigned int ref_dstAlloc;
unsigned int ref_srcAlloc;
unsigned int* dstBuffer;
unsigned int* srcBuffer;
unsigned int* readBuffers[NUM_TAGS];
unsigned int* writeBuffers[NUM_TAGS];
TagTableEntry readTagTable[NUM_TAGS];
TagTableEntry writeTagTable[NUM_TAGS];
TagTableEntry eraseTagTable[NUM_TAGS];
FlashStatusT flashStatus[NUM_BUSES][CHIPS_PER_BUS][BLOCKS_PER_CHIP];

// for Table 
#define NUM_BLOCKS 4096
#define NUM_SEGMENTS NUM_BLOCKS
#define NUM_CHANNELS 8
#define NUM_CHIPS 8
#define NUM_LOGBLKS (NUM_CHANNELS*NUM_CHIPS)

size_t blkmapAlloc_sz = sizeof(uint16_t) * NUM_SEGMENTS * NUM_LOGBLKS;
int blkmapAlloc;
uint ref_blkmapAlloc;
uint16_t (*blkmap)[NUM_CHANNELS*NUM_CHIPS]; // 4096*64
uint16_t (*blkmgr)[NUM_CHIPS][NUM_BLOCKS];  // 8*8*4096

// temp
bdbm_sema_t global_lock;
/***/


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
} dm_nohost_private_t;

dm_nohost_private_t* _priv = NULL;

/* global data structure */
extern bdbm_drv_info_t* _bdi_dm;


class FlashIndication: public FlashIndicationWrapper {
	public:
		FlashIndication (unsigned int id) : FlashIndicationWrapper (id) { }

		virtual void readDone (unsigned int tag, unsigned int status) {
			printf ("LOG: readdone: tag=%d status=%d\n", tag, status); fflush (stdout);
			dm_nohost_end_req (_bdi_dm, _priv->llm_reqs[tag]);
			_priv->llm_reqs[tag] = NULL;
			bdbm_sema_unlock (&global_lock);
		}

		virtual void writeDone (unsigned int tag, unsigned int status) {
			printf ("LOG: writedone: tag=%d status=%d\n", tag, status); fflush (stdout);
			dm_nohost_end_req (_bdi_dm, _priv->llm_reqs[tag]);
			_priv->llm_reqs[tag] = NULL;
			bdbm_sema_unlock (&global_lock);
		}

		virtual void eraseDone (unsigned int tag, unsigned int status) {
			printf ("LOG: eraseDone, tag=%d, status=%d\n", tag, status); fflush(stdout);
			dm_nohost_end_req (_bdi_dm, _priv->llm_reqs[tag]);
			_priv->llm_reqs[tag] = NULL;
			bdbm_sema_unlock (&global_lock);
		}

		virtual void debugDumpResp (unsigned int debug0, unsigned int debug1,  unsigned int debug2, unsigned int debug3, unsigned int debug4, unsigned int debug5) {
			fprintf(stderr, "LOG: DEBUG DUMP: gearSend = %d, gearRec = %d, aurSend = %d, aurRec = %d, readSend=%d, writeSend=%d\n", debug0, debug1, debug2, debug3, debug4, debug5);
		}

		virtual void uploadDone () {
			fprintf(stderr, "Map Upload(Host->FPGA) done!\n");
		}

		virtual void downloadDone() {
			fprintf(stderr, "Map Download(FPGA->Host) done!\n");
		}
};

FlashIndication* indication;
uint32_t __dm_nohost_init_device (
	bdbm_drv_info_t* bdi, 
	bdbm_device_params_t* params)
{
	fprintf(stderr, "Initializing DMA...\n");
	device = new FlashRequestProxy(IfcNames_FlashRequestS2H);
	indication = new FlashIndication(IfcNames_FlashIndicationH2S);
	//FlashIndication deviceIndication(IfcNames_FlashIndicationH2S);
    DmaManager *dma = platformInit();

	fprintf(stderr, "Main::allocating memory...\n");
	srcAlloc = portalAlloc(srcAlloc_sz, 0);
	dstAlloc = portalAlloc(dstAlloc_sz, 0);
	srcBuffer = (unsigned int *)portalMmap(srcAlloc, srcAlloc_sz);
	dstBuffer = (unsigned int *)portalMmap(dstAlloc, dstAlloc_sz);

	blkmapAlloc = portalAlloc(blkmapAlloc_sz*2, 0);
	char *tmpPtr = (char*)portalMmap(blkmapAlloc, blkmapAlloc_sz*2);
	blkmap      = (uint16_t(*)[NUM_CHANNELS*NUM_CHIPS]) (tmpPtr);
	blkmgr      = (uint16_t(*)[NUM_CHIPS][NUM_BLOCKS])  (tmpPtr+blkmapAlloc_sz);

	fprintf(stderr, "dstAlloc = %x\n", dstAlloc); 
	fprintf(stderr, "srcAlloc = %x\n", srcAlloc); 
	fprintf(stderr, "blkmapAlloc = %x\n", blkmapAlloc); 
	
	portalCacheFlush(dstAlloc, dstBuffer, dstAlloc_sz, 1);
	portalCacheFlush(srcAlloc, srcBuffer, srcAlloc_sz, 1);
	portalCacheFlush(blkmapAlloc, blkmap, blkmapAlloc_sz*2, 1);

	ref_dstAlloc = dma->reference(dstAlloc);
	ref_srcAlloc = dma->reference(srcAlloc);
	ref_blkmapAlloc = dma->reference(blkmapAlloc);

	device->setDmaWriteRef(ref_dstAlloc);
	device->setDmaReadRef(ref_srcAlloc);
	device->setDmaMapRef(ref_blkmapAlloc);

	for (int t = 0; t < NUM_TAGS; t++) {
		readTagTable[t].busy = false;
		writeTagTable[t].busy = false;
		int byteOffset = t * FPAGE_SIZE;
		readBuffers[t] = dstBuffer + byteOffset/sizeof(unsigned int);
		writeBuffers[t] = srcBuffer + byteOffset/sizeof(unsigned int);
	}
	
	for (int blk=0; blk < BLOCKS_PER_CHIP; blk++) {
		for (int c=0; c < CHIPS_PER_BUS; c++) {
			for (int bus=0; bus< NUM_BUSES; bus++) {
				flashStatus[bus][c][blk] = UNINIT;
			}
		}
	}

	for (int t = 0; t < NUM_TAGS; t++) {
		for ( unsigned int i = 0; i < FPAGE_SIZE/sizeof(unsigned int); i++ ) {
			readBuffers[t][i] = 0xDEADBEEF;
			writeBuffers[t][i] = 0xBEEFDEAD;
		}
	}

#define MainClockPeriod 5

	long actualFrequency=0;
	long requestedFrequency=1e9/MainClockPeriod;
	int status = setClockFrequency(0, requestedFrequency, &actualFrequency);
	fprintf(stderr, "Requested Freq: %5.2f, Actual Freq: %5.2f, status=%d\n"
			,(double)requestedFrequency*1.0e-6
			,(double)actualFrequency*1.0e-6,status);

	device->start(0);
	device->setDebugVals(0,0); //flag, delay

	device->debugDumpReq(0);
	sleep(1);
	device->debugDumpReq(0);
	sleep(1);

	for (int t = 0; t < NUM_TAGS; t++) {
		for ( unsigned int i = 0; i < FPAGE_SIZE/sizeof(unsigned int); i++ ) {
			readBuffers[t][i] = 0xDEADBEEF;
		}
	}

	//device->downloadMap();
	//device->uploadMap();
	
	return 0;
}

uint32_t dm_nohost_probe (
	bdbm_drv_info_t* bdi, 
	bdbm_device_params_t* params)
{
	dm_nohost_private_t* p = NULL;
	uint32_t nr_punit;

	bdbm_msg ("[dm_nohost_probe] PROBE STARTED");

	/* setup NAND parameters according to users' inputs */
	*params = get_default_device_params ();

	/* create a private structure for ramdrive */
	if ((p = (dm_nohost_private_t*)bdbm_malloc
			(sizeof (dm_nohost_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		goto fail;
	}

	/* initialize the nohost device */
	if (__dm_nohost_init_device (bdi, params) != 0) {
		bdbm_error ("__dm_nohost_init_device() failed");
		bdbm_free (p);
		goto fail;
	}

	bdbm_sema_init (&global_lock);
	nr_punit = params->nr_channels * params->nr_chips_per_channel;
	if ((p->llm_reqs = (bdbm_llm_req_t**)bdbm_zmalloc (
			sizeof (bdbm_llm_req_t*) * nr_punit)) == NULL) {
		bdbm_warning ("bdbm_zmalloc failed");
		goto fail;
	}

	/* OK! keep private info */
	bdi->ptr_dm_inf->ptr_private = (void*)p;
	_priv = p;

	bdbm_msg ("[dm_nohost_probe] PROBE DONE!");

	return 0;

fail:
	return -1;
}

uint32_t dm_nohost_open (bdbm_drv_info_t* bdi)
{
	dm_nohost_private_t * p = (dm_nohost_private_t*)BDBM_DM_PRIV (bdi);

	bdbm_msg ("[dm_nohost_open] open done!");

	return 0;
}

void dm_nohost_close (bdbm_drv_info_t* bdi)
{
	dm_nohost_private_t* p = (dm_nohost_private_t*)BDBM_DM_PRIV (bdi);

	bdbm_msg ("[dm_nohost_close] closed!");

	bdbm_free (p);
}

uint32_t dm_nohost_make_req (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r)
{
	uint32_t punit_id, ret, i;
	dm_nohost_private_t* priv = (dm_nohost_private_t*)BDBM_DM_PRIV (bdi);

	bdbm_msg ("dm_nohost_make_req - begin");
	bdbm_sema_lock (&global_lock);
	if (priv->llm_reqs[r->tag] != NULL) {
		bdbm_sema_unlock (&global_lock);
		bdbm_error ("tag (%u) is busy...", r->tag);
		bdbm_bug_on (1);
		return -1;
	} else {
		priv->llm_reqs[r->tag] = r;
	}
	bdbm_sema_unlock (&global_lock);

	/* submit reqs to the device */
	switch (r->req_type) {
	case REQTYPE_WRITE:
	case REQTYPE_RMW_WRITE:
	case REQTYPE_GC_WRITE:
	case REQTYPE_META_WRITE:
		printf ("LOG: device->writePage, tag=%d lpa=%d\n", r->tag, r->logaddr.lpa[0]); fflush(stdout);
		device->writePage (r->tag, r->logaddr.lpa[0], r->tag * FPAGE_SIZE);
		break;

	case REQTYPE_READ:
	case REQTYPE_READ_DUMMY:
	case REQTYPE_RMW_READ:
	case REQTYPE_GC_READ:
	case REQTYPE_META_READ:
		printf ("LOG: device->readPage, tag=%d lap=%d\n", r->tag, r->logaddr.lpa[0]); fflush(stdout);
		device->readPage (r->tag, r->logaddr.lpa[0], r->tag * FPAGE_SIZE);
		break;

	case REQTYPE_GC_ERASE:
		printf ("LOG: device->eraseBlock, tag=%d lpa=%d\n", r->tag, r->logaddr.lpa[0]); fflush(stdout);
		device->eraseBlock (r->tag, r->logaddr.lpa[0]);
		break;

	default:
		bdbm_sema_unlock (&global_lock);
		break;
	}

	return 0;
}

void dm_nohost_end_req (
	bdbm_drv_info_t* bdi, 
	bdbm_llm_req_t* r)
{
	bdbm_bug_on (r == NULL);

	bdbm_msg ("dm_nohost_end_req done");
	bdi->ptr_llm_inf->end_req (bdi, r);
}

