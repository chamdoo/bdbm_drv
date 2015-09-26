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

/* DESCRIPTION: This is an example code of showing how a kernel-module accesses
 * bdbm devices directly using low-level flash commands (e.g, page-read,
 * page-write, and block-erase) without the FTL.
 * 
 * With the raw-flash interface, the end-users should handle everything for
 * flash management, which includes address remapping, bad-block management,
 * garbage collection, and wear-leveling */

#include <linux/kernel.h>
#include "raw-flash.h"

bdbm_raw_flash_t* rf = NULL;

static int __init raw_flash_init (void)
{
	if ((rf = bdbm_raw_flash_init ()) == NULL)
		return -1;

	if (bdbm_raw_flash_open (rf) != 0)
		return -1;

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
