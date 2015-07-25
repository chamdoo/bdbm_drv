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

#include "bdbm_drv.h"
#include "debug.h"
#include "platform.h"
#include "host_user.h"
#include "params.h"
/*#include "ioctl.h"*/

#include "utils/utime.h"

struct bdbm_host_inf_t _host_user_inf = {
	.ptr_private = NULL,
	.open = host_user_open,
	.close = host_user_close,
	.make_req = host_user_make_req,
	.end_req = host_user_end_req,
};

struct bdbm_host_user_private {
	uint64_t nr_host_reqs;
	bdbm_spinlock_t lock;
};

uint32_t host_user_open (struct bdbm_drv_info* bdi)
{
	return -1;
}

void host_user_close (struct bdbm_drv_info* bdi)
{
}

void host_user_make_req (struct bdbm_drv_info* bdi, void *bio)
{
}

void host_user_end_req (struct bdbm_drv_info* bdi, struct bdbm_hlm_req_t* req)
{
}

