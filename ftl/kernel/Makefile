# Makefile for a RISA Device Driver
#

SRC := ../src

KBUILD_EXTRA_SYMBOLS := \
	$(PWD)/../../hw/ramdrive/Module.symvers \

EXTRA_CFLAGS := \
	-I$(PWD)/../../include \
	-I$(PWD)/../../include/utils \
	-I$(PWD)/../src \

EXTRA_CFLAGS += -D HASH_BLOOM=20 	# for HASH (8KB)
EXTRA_CFLAGS += -D CONFIG_ENABLE_MSG
EXTRA_CFLAGS += -D CONFIG_ENABLE_DEBUG
EXTRA_CFLAGS += -D USE_PMU
EXTRA_CFLAGS += -D USE_KTIMER
EXTRA_CFLAGS += -D KERNEL_MODE
#EXTRA_CFLAGS += -D SNAPSHOT_ENABLE
#EXTRA_CFLAGS += -D EMULATE_BAD_BLOCKS

bdbm_drv-y := \
	../../include/utils/utime.o \
	../../include/utils/ufile.o \
	$(SRC)/kparams.o \
	$(SRC)/ioctl.o \
	$(SRC)/kmain.o \
	$(SRC)/pmu.o \
	$(SRC)/host_block.o \
	$(SRC)/hlm_nobuf.o \
	$(SRC)/hlm_buf.o \
	$(SRC)/hlm_rsd.o \
	$(SRC)/llm_noq.o \
	$(SRC)/llm_mq.o \
	$(SRC)/algo/abm.o \
	$(SRC)/algo/no_ftl.o \
	$(SRC)/algo/block_ftl.o \
	$(SRC)/algo/page_ftl.o \
	$(SRC)/queue/queue.o \
	$(SRC)/queue/prior_queue.o

obj-m := bdbm_drv.o

ccflags-y := -I$(src)

export KROOT=/lib/modules/$(shell uname -r)/build

.PHONY: default
default: modules

.PHONY: modules
modules:
	@$(MAKE) -C $(KROOT) M=$(PWD) modules

.PHONY: modules_check
modules_check:
	@$(MAKE) -C $(KROOT) C=2 M=$(PWD) modules

.PHONY: modules_install
modules_install:
	@$(MAKE) -C $(KROOT) M=$(PWD) modules_install

.PHONY: kernel_clean
kernel_clean:
	@$(MAKE) -C $(KROOT) M=$(PWD) clean
 
.PHONY: clean
clean: kernel_clean
	@rm -rf Module.markers modules.order
	@cd ../src; rm -rf *.o .*.cmd

