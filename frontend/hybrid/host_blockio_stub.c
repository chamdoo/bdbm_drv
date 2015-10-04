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
#include <stdint.h>
#include <unistd.h>

#include "bdbm_drv.h"
#include "debug.h"
#include "platform.h"
#include "params.h"
#include "utils/utime.h"
#include "utils/uthread.h"
#include "host_blockio_stub.h"


bdbm_host_inf_t _host_blockio_stub_inf = {
	.ptr_private = NULL,
	.open = blockio_stub_open,
	.close = blockio_stub_close,
	.make_req = blockio_stub_make_req,
	.end_req = blockio_stub_end_req,
};

uint32_t blockio_stub_open (bdbm_drv_info_t* bdi)
{
	return 0;
}

void blockio_stub_close (bdbm_drv_info_t* bdi)
{
}

void blockio_stub_make_req (bdbm_drv_info_t* bdi, void* bio)
{
}

void blockio_stub_end_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* req)
{
}

