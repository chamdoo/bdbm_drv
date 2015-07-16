# Makefile for a RISA Device Driver
#

KBUILD_EXTRA_SYMBOLS := \
	$(PWD)/../devices/ramdrive/Module.symvers \

EXTRA_CFLAGS += -D HASH_BLOOM=20 	# for HASH (8KB)
EXTRA_CFLAGS += -D CONFIG_ENABLE_MSG
EXTRA_CFLAGS += -D CONFIG_ENABLE_DEBUG
EXTRA_CFLAGS += -D USE_PMU
EXTRA_CFLAGS += -D USE_KTIMER
#EXTRA_CFLAGS += -D SNAPSHOT_ENABLE
#EXTRA_CFLAGS += -D EMULATE_BAD_BLOCKS

bdbm_drv-y := \
	params.o \
	ioctl.o \
	kmain.o \
	pmu.o \
	host_block.o \
	hlm_nobuf.o \
	hlm_buf.o \
	hlm_rsd.o \
	llm_noq.o \
	llm_mq.o \
	../../include/utils/time.o \
	../../include/utils/file.o \
	ftl/abm.o \
	ftl/no_ftl.o \
	ftl/block_ftl.o \
	ftl/page_ftl.o \
	queue/queue.o \
	queue/prior_queue.o

obj-m := bdbm_drv.o

ccflags-y := -I$(src)/$(CPPDIR) -I$(src)/$(BOARDDIR) $(HARDWARE_FLAGS) -DBOARD_$(BOARD)

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
	rm -rf Module.markers modules.order

