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


/* DESCRIPTION: This is an example code that shows how a kernel-module uses the
 * bdbm devices directly with low-level flash commands (e.g, page-read,
 * page-write, and block-erase) without the FTL.
 * 
 * With the raw-flash interface, the end-users must handle everything about
 * flash management, including address remapping, bad-block management, garbage
 * collection, and wear-leveling */

#include <linux/kernel.h>
#include "raw-flash.h"

bdbm_raw_flash_t* rf = NULL;


static void run_async_test (nand_params_t* np)
{
	int channel = 0, chip = 0, block = 0, page = 0, lpa = 0, tagid = 0;
	uint8_t** main_page = NULL;
	uint8_t** oob_page = NULL;


	/* alloc memory */
	printk (KERN_INFO "[run_async_test] alloc memory");
	main_page = (uint8_t**)vmalloc (np->nr_channels * np->nr_chips_per_channel * sizeof (uint8_t*));
	oob_page = (uint8_t**)vmalloc (np->nr_channels * np->nr_chips_per_channel * sizeof (uint8_t*));
	for (chip = 0; chip < np->nr_chips_per_channel; chip++) {
		for (channel = 0; channel < np->nr_channels; channel++) {
			tagid = np->nr_chips_per_channel * channel + chip;
			main_page[tagid] = (uint8_t*)vmalloc (np->page_main_size * sizeof (uint8_t));
			oob_page[tagid] = (uint8_t*)vmalloc (np->page_oob_size * sizeof (uint8_t));
		}
	}

	/* send erase reqs */

	/* send reqs */
	printk (KERN_INFO "[run_async_test] send reqs");
	for (page = 0; page < np->nr_pages_per_block; page++) {
		for (block = 0; block < np->nr_blocks_per_chip; block++) {
			for (chip = 0; chip < np->nr_chips_per_channel; chip++) {
				for (channel = 0; channel < np->nr_channels; channel++) {
					tagid = np->nr_chips_per_channel * channel + chip;

					/* sleep if it is bysy */
					bdbm_raw_flash_wait (
						rf, 
						channel, 
						chip);

					/* send a request */
					bdbm_raw_flash_read_page_async (
						rf, 
						channel, chip, block, page, lpa,
						main_page[tagid],
						oob_page[tagid]);

					lpa++;
				}
			}
		}
	}

	/* free memory */
	printk (KERN_INFO "[run_async_test] free memory");
	for (chip = 0; chip < np->nr_chips_per_channel; chip++) {
		for (channel = 0; channel < np->nr_channels; channel++) {
			tagid = np->nr_chips_per_channel * channel + chip;

			/* sleep if it is bysy */
			bdbm_raw_flash_wait (rf, channel, chip);

			vfree (main_page[tagid]);
			vfree (oob_page[tagid]);
		}
	}

	vfree (main_page);
	vfree (oob_page);
}

static int __init raw_flash_init (void)
{
	nand_params_t* np;

	if ((rf = bdbm_raw_flash_init ()) == NULL)
		return -1;

	if (bdbm_raw_flash_open (rf) != 0)
		return -1;

	if ((np = bdbm_raw_flash_get_nand_params (rf)) == NULL)
		return -1;

	/* do a test */
	run_async_test (np);
	/* done */

	return 0;
}

static void __exit raw_flash_exit (void)
{
	bdbm_raw_flash_exit (rf);
}

MODULE_AUTHOR ("Sungjin Lee <chamdoo@csail.mit.edu>");
MODULE_DESCRIPTION ("BlueDBM Manage-Flash Example");
MODULE_LICENSE ("GPL");

module_init (raw_flash_init);
module_exit (raw_flash_exit);
