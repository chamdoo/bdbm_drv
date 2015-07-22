# Makefile for BlueDBM
#

EXTRA_CFLAGS := \
	-I$(PWD)/../../include \
	-I$(PWD)/../../include/utils \
	-I$(PWD)/../cpp \

EXTRA_CFLAGS += \
	-D CONFIG_ENABLE_MSG \
	-D CONFIG_ENABLE_DEBUG \
	-D CONFIG_DEVICE_TYPE_RAMDRIVE_TIMING \
	-D KERNEL_MODE

risa_dev_ramdrive_timing-y := \
	../../include/utils/ufile.o \
	../../include/utils/utime.o \
	../cpp/dm_main.o \
	../cpp/dm_params.o \
	../ramdrive/dev_ramssd.o \
	../ramdrive/dm_ramdrive.o \

obj-m := risa_dev_ramdrive_timing.o

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

