# Makefile for BlueDBM
#

BOARD = vc707
HARDWARE_FLAGS = -D VC707
EXTRA_CFLAGS = -D BOARD_VC707
BOARDDIR = $(BOARD)/jni
CPPDIR = connectal/cpp

KBUILD_EXTRA_SYMBOLS := \
	$(PWD)/connectal/drivers/pcieportal/Module.symvers \
	$(PWD)/connectal/drivers/portalmem/Module.symvers

EXTRA_CFLAGS := \
	-D NO_CPP_PORTAL_CODE \
	-D __KERNEL__ \
	-D MMAP_HW \
	-I$(PWD) \
	-I$(PWD)/connectal \
	-I$(PWD)/connectal/cpp \
	-I$(PWD)/connectal/drivers/pcieportal \
	-I$(PWD)/connectal/drivers/portalmem \
	-I$(PWD)/../../include \
	-I$(PWD)/../../include/utils \
	-I$(PWD)/../cpp \

EXTRA_CFLAGS += \
	-D CONFIG_ENABLE_MSG \
	-D CONFIG_ENABLE_DEBUG \
	-D CONFIG_DEVICE_TYPE_BLUEDBM \
	-D KERNEL_MODE \

risa_dev_bluedbm-y := \
	../cpp/dm_main.o \
	../cpp/dm_params.o \
	../cpp/dm_stub.o \
	dm_bluedbm.o \
	$(BOARDDIR)/MMURequest.o \
	$(BOARDDIR)/MMUIndication.o \
	$(BOARDDIR)/FlashIndication.o \
	$(BOARDDIR)/FlashRequest.o \
	$(BOARDDIR)/MemServerRequest.o \
	$(CPPDIR)/portal.o \
	$(CPPDIR)/dmaManager.o \

obj-m := risa_dev_bluedbm.o

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

