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
#include <linux/moduleparam.h>
#include <linux/slab.h>

#elif defined (USER_MODE)

#include <stdio.h>
#include <stdint.h>

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#include "params.h"
#include "platform.h"

int _param_nr_channels 				= NR_CHANNELS;
int _param_nr_chips_per_channel		= NR_CHIPS_PER_CHANNEL;
int _param_nr_blocks_per_chip 		= NR_BLOCKS_PER_CHIP;
int _param_nr_pages_per_block 		= NR_PAGES_PER_BLOCK;
int _param_page_main_size 			= NAND_PAGE_SIZE;
int _param_page_oob_size 			= NAND_PAGE_OOB_SIZE;
int _param_host_bus_trans_time_us	= NAND_HOST_BUS_TRANS_TIME_US;
int _param_chip_bus_trans_time_us	= NAND_CHIP_BUS_TRANS_TIME_US;
int _param_page_prog_time_us		= NAND_PAGE_PROG_TIME_US; 		
int _param_page_read_time_us		= NAND_PAGE_READ_TIME_US;
int _param_block_erase_time_us		= NAND_BLOCK_ERASE_TIME_US;

/* TODO: Hmm... there might be a more fancy way than this... */
#if defined (CONFIG_DEVICE_TYPE_RAMDRIVE)
int	_param_device_type = DEVICE_TYPE_RAMDRIVE;
#elif defined (CONFIG_DEVICE_TYPE_RAMDRIVE_INTR)
int	_param_device_type = DEVICE_TYPE_RAMDRIVE_INTR;
#elif defined (CONFIG_DEVICE_TYPE_RAMDRIVE_TIMING)
int _param_device_type = DEVICE_TYPE_RAMDRIVE_TIMING;
#elif defined (CONFIG_DEVICE_TYPE_BLUEDBM)
int _param_device_type = DEVICE_TYPE_BLUEDBM;
#elif defined (CONFIG_DEVICE_TYPE_USER_DUMMY)
int _param_device_type = DEVICE_TYPE_USER_DUMMY;
#elif defined (CONFIG_DEVICE_TYPE_USER_RAMDRIVE)
int _param_device_type = DEVICE_TYPE_USER_RAMDRIVE;
#else
#error Invalid HW is set
int _param_device_type = DEVICE_TYPE_NOTSET;
#endif

#if defined (KERNEL_MODE)
module_param (_param_nr_channels, int, 0000);
module_param (_param_nr_chips_per_channel, int, 0000);
module_param (_param_nr_blocks_per_chip, int, 0000);
module_param (_param_nr_pages_per_block, int, 0000);
module_param (_param_page_main_size, int, 0000);
module_param (_param_page_oob_size, int, 0000);
module_param (_param_host_bus_trans_time_us, int, 0000);
module_param (_param_chip_bus_trans_time_us, int, 0000);
module_param (_param_page_prog_time_us, int, 0000);
module_param (_param_page_read_time_us, int, 0000);
module_param (_param_block_erase_time_us, int, 0000);
module_param (_param_device_type, int, 0000);

MODULE_PARM_DESC (_param_nr_channels, "# of channels");
MODULE_PARM_DESC (_param_nr_chips_per_channel, "# of chips per channel");
MODULE_PARM_DESC (_param_nr_blocks_per_chip, "# of blocks per chip");
MODULE_PARM_DESC (_param_nr_pages_per_block, "# of pages per block");
MODULE_PARM_DESC (_param_page_main_size, "page main size");
MODULE_PARM_DESC (_param_page_oob_size, "page oob size");
MODULE_PARM_DESC (_param_ramdrv_timing_mode, "timing mode for ramdrive");
MODULE_PARM_DESC (_param_host_bus_trans_time_us, "host bus transfer time");
MODULE_PARM_DESC (_param_chip_bus_trans_time_us, "NAND bus transfer time");
MODULE_PARM_DESC (_param_page_prog_time_us, "page program time");
MODULE_PARM_DESC (_param_page_read_time_us, "page read time");
MODULE_PARM_DESC (_param_block_erase_time_us, "block erasure time");
MODULE_PARM_DESC (_param_device_type, "device type"); /* it must be reset when implementing actual device modules */
#endif

