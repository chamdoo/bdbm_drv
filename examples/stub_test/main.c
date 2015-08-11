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
#include <sys/mman.h> /* mmap */

#include "bdbm_drv.h"
#include "dm_stub.h"
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
	struct pollfd fds[1];
	uint8_t* punit_status = NULL;
	struct nand_params np;

	if ((fd = open (BDBM_DM_IOCTL_DEVNAME, O_RDWR)) < 0) {
		printf ("error: could not open a character device (re = %d)\n", fd);
		return -1;
	}

	/* check probe */
	{
		ret = ioctl (fd, BDBM_DM_IOCTL_PROBE, &np);
		printf ("probe (): ret = %d\n", ret);

		printf ("nr_channels: %u\n", (uint32_t)np.nr_channels);
		printf ("nr_chips_per_channel: %u\n", (uint32_t)np.nr_chips_per_channel);
		printf ("nr_blocks_per_chip: %u\n", (uint32_t)np.nr_blocks_per_chip);
		printf ("nr_pages_per_block: %u\n", (uint32_t)np.nr_pages_per_block);
		printf ("page_main_size: %u\n", (uint32_t)np.page_main_size);
		printf ("page_oob_size: %u\n", (uint32_t)np.page_oob_size);
		printf ("device_type: %u\n", (uint32_t)np.device_type);
	}

	/* check open */
	{
		int result;
		ret = ioctl (fd, BDBM_DM_IOCTL_OPEN, &result);
		printf ("open (): result = %d, ret = %d\n", result, ret);
	}

	/* check mmap */
	{
		punit_status = mmap (NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		printf ("punit_status: %p\n", punit_status);
		printf ("punit_status[0] = %u\n", punit_status[0]);
	}

	/* check make_req */
	{
		struct bdbm_llm_req_t* r;
		uint64_t punit_id;

		r = create_llm_req ();
		punit_id = r->phyaddr->channel_no *
			np.nr_chips_per_channel +
	  		r->phyaddr->chip_no;

		ret = ioctl (fd, BDBM_DM_IOCTL_MAKE_REQ, r);
		printf ("make_req () ret = %u\n", ret);

		fds[0].fd = fd;
		fds[0].events = POLLIN;
		poll (fds, 1, -1);

		printf ("end_req () ret = %u\n", r->ret);
		delete_llm_req (r);

		printf ("punit_status[0] = %u\n", punit_status[punit_id]);
		punit_status[0] = 0;
	}

	/* check close */
	{
		int result;
		ret = ioctl (fd, BDBM_DM_IOCTL_CLOSE, &result);
		printf ("close (): result = %d, ret = %d\n", result, ret);
	}

	close (fd);

	return 0;
}
