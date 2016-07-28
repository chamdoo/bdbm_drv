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

#ifndef _BLUEDBM_DEV_HYNIX_NVME_H
#define _BLUEDBM_DEV_HYNIX_NVME_H

#include "bdbm_drv.h"

typedef struct {
	struct request_queue *q;
	struct request_queue *queue;
	struct gendisk *gd;
} dumb_ssd_dev_t;

typedef struct {
	dumb_ssd_dev_t* dev;
	bdbm_llm_req_t* r;
	int rw;
	uint64_t die;
	uint64_t block;
	uint64_t wu;
	uint8_t* kp_ptr;
	uint8_t* buffer;
	void (*intr_handler)(void*);
} hd_req_t;

uint32_t dev_hynix_nvme_submit_io (dumb_ssd_dev_t* dev, bdbm_llm_req_t* r, void (*intr_handler)(void*));
int simple_write (dumb_ssd_dev_t* dev, hd_req_t* hdr);
int simple_read (dumb_ssd_dev_t* dev, hd_req_t* hdr);
int simple_erase (dumb_ssd_dev_t* dev, hd_req_t* hdr);

#endif
