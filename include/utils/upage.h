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

#ifndef _UPAGE_H
#define _UPAGE_H

#if defined (KERNEL_MODE)
#error upage.h is not intended for the use in the kernel mode
#endif

#include <stdio.h>
#include <stdlib.h>

#define GFP_KERNEL	0

unsigned long get_zeroed_page (int gfp_mask) {
	void* ptr_page = NULL;

	ptr_page = (void*)malloc (4086);
	if (ptr_page == NULL) {
		printf ("CRITICAL-ERROR: malloc failed at %d%s\n", __LINE__, __FILE__);
	}

	return (unsigned long)ptr_page;
}

void free_page (unsigned long addr) {
	void* ptr_page = (void*)addr;
	free (ptr_page);
}

#endif

