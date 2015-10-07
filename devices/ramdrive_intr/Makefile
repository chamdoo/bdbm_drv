# Makefile for BlueDBM
#

INCLUDE = ../../include
LIB = ../../lib
COMMON = ../common

EXTRA_CFLAGS := \
	-I$(PWD)/$(INCLUDE) \
	-I$(PWD)/$(LIB)/utils \
	-I$(PWD)/$(LIB)/3rd \
	-I$(PWD)/$(COMMON) \

EXTRA_CFLAGS += \
	-D CONFIG_ENABLE_MSG \
	-D CONFIG_ENABLE_DEBUG \
	-D CONFIG_DEVICE_TYPE_RAMDRIVE_INTR \
	-D KERNEL_MODE

risa_dev_ramdrive_intr-y := \
	$(LIB)/utils/ufile.o \
	$(LIB)/utils/utime.o \
	$(COMMON)/dm_main.o \
	$(COMMON)/dm_params.o \
	$(COMMON)/dm_stub.o \
	../ramdrive/dev_ramssd.o \
	../ramdrive/dm_ramdrive.o \

obj-m := risa_dev_ramdrive_intr.o

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

