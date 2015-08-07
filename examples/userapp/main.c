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
#include <fcntl.h> /* O_RDWR */
#include <unistd.h> /* close */
#include <poll.h> /* poll */

#include "bdbm_drv.h"
#include "dm_ioctl.h"
#include "hw.h"

/*nr_kp_per_fp = np->page_main_size / KERNEL_PAGE_SIZE;*/
int nr_kp_per_fp = 1;

struct bdbm_llm_req_t* create_llm_req (void)
{
	int loop = 0;
	struct bdbm_llm_req_t* r = NULL;

	r = (struct bdbm_llm_req_t*)malloc (sizeof (struct bdbm_llm_req_t));

	/* items to copy */
	r->req_type = REQTYPE_READ;
	r->lpa = 10;
	r->phyaddr = &r->phyaddr_r;
	r->kpg_flags = (uint8_t*)malloc (sizeof (uint8_t) * nr_kp_per_fp);
	r->kpg_flags[0] = 6;
	r->pptr_kpgs = (uint8_t**)malloc (sizeof (uint8_t*) * nr_kp_per_fp); 
	for (loop = 0; loop < nr_kp_per_fp; loop++) {
		r->pptr_kpgs[loop] = malloc (KERNEL_PAGE_SIZE * sizeof (uint8_t));
		r->pptr_kpgs[loop][0] = 0xA;
		r->pptr_kpgs[loop][1] = 0xB;
		r->pptr_kpgs[loop][2] = 0xC;
	}
	r->ptr_oob = (uint8_t*)malloc (sizeof (uint8_t) * 64);

	/* items to receive */
	r->ret = 0;

	/* items to ignore */
	r->phyaddr_r.channel_no = 1;
	r->phyaddr_r.chip_no = 2;
	r->phyaddr_r.block_no = 3;
	r->phyaddr_r.page_no = 4;
	r->phyaddr_w = r->phyaddr_r;
	/*r->ptr_hlm_req = NULL;*/
	/*r->list;*/
	/*r->ptr_qitem = NULL;*/

	return r;
}

void delete_llm_req (struct bdbm_llm_req_t* r)
{
	int loop = 0;

	free (r->ptr_oob);
	for (loop = 0; loop < nr_kp_per_fp; loop++) {
		free (r->pptr_kpgs[loop]);
	}
	free (r->pptr_kpgs);
	free (r->kpg_flags);
	free (r);
}

int main (int argc, char** argv)
{
	int fd = -1;
	int ret = 0;
	char* devname = "/dev/bdbm_dm_char";
	struct pollfd fds[1];
	struct bdbm_llm_req_t* r;

	if ((fd = open (devname, O_RDWR)) < 0) {
		printf ("error: could not open a character device (re = %d)\n", fd);
		return -1;
	}

	printf ("about to send an ictl command\n");
	r = create_llm_req ();
	if ((ret = ioctl (fd, BDBM_DM_IOCTL_MAKE_REQ, r)) < 0) {
		printf ("sent an ictl command\n");
	} else {
		printf ("ret = %u\n", ret);
	}

	fds[0].fd = fd;
	fds[0].events = POLLIN;
	poll (fds, 1, -1);

	printf ("get an ack from a device (ret = %u)\n", r->ret);
	delete_llm_req (r);

	close (fd);

	return 0;
}
