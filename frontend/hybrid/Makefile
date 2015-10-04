# Makefile for a RISA Device Driver
#

FTL := ../../ftl
COMMON := ../../include
KERN := ../kernel

EXTRA_CFLAGS := \
	-I$(PWD)/../../include \
	-I$(PWD)/../../include/utils \
	-I$(PWD)/$(FTL) \
	-I$(PWD)/$(KERN) \

EXTRA_CFLAGS += -D HASH_BLOOM=20 	# for HASH (8KB)
EXTRA_CFLAGS += -D CONFIG_ENABLE_MSG
EXTRA_CFLAGS += -D CONFIG_ENABLE_DEBUG
EXTRA_CFLAGS += -D KERNEL_MODE
EXTRA_CFLAGS += -D USE_PMU
EXTRA_CFLAGS += -D USE_BLOCKIO_PROXY
#EXTRA_CFLAGS += -D USE_KTIMER

bdbm_drv-y := \
	host_blockio_proxy.o \
	proxy_reqs_pool.o \
	$(KERN)/kmain.o \
	$(KERN)/kparams.o \
	$(KERN)/host_blkdev.o \
	$(COMMON)/utils/utime.o \
	$(COMMON)/utils/ufile.o \
	$(COMMON)/utils/uthread.o \
	$(FTL)/pmu.o \

obj-m := bdbm_drv.o

ccflags-y := -I$(src)

export KROOT=/lib/modules/$(shell uname -r)/build

.PHONY: default
default: kernel user	

kernel: 
	@$(MAKE) -f Makefile.kernel

user:
	@$(MAKE) -f Makefile.user

.PHONY: clean 
clean: clean-kernel clean-user 

clean-kernel:
	@$(MAKE) -f Makefile.kernel clean

clean-user:
	@$(MAKE) -f Makefile.user clean

mount:
	sudo insmod bdbm_drv.ko

umount:
	sudo rmmod bdbm_drv
