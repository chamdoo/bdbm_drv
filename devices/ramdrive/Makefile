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
	-D CONFIG_DEVICE_TYPE_RAMDRIVE \
	-D KERNEL_MODE

risa_dev_ramdrive-y := \
	$(LIB)/utils/ufile.o \
	$(LIB)/utils/utime.o \
	$(LIB)/utils/uthread.o \
	$(COMMON)/dm_main.o \
	$(COMMON)/dm_params.o \
	$(COMMON)/dm_stub.o \
	dev_ramssd.o \
	dm_ramdrive.o \

obj-m := risa_dev_ramdrive.o

export KROOT=/lib/modules/$(shell uname -r)/build

.PHONY: default
default: modules

.PHONY: modules
modules:
	@$(MAKE) -C $(KROOT) M=$(PWD) modules
	@chmod +x risa_dev_ramdrive.ko

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
	@$(RM) -rf Module.markers modules.order
	@cd ../cpp; rm *.o

