# Makefile for a RISA Device Driver
#
CC = arm-linux-gnueabihf-g++
AR = arm-linux-gnueabihf-ar

ROOTDIR = ../..
INCLUDE = $(ROOTDIR)/include
COMMON = $(ROOTDIR)/common
DM_COMMON = $(ROOTDIR)/devices/common
BOARD = zc706_ubuntu
BOARDDIR = $(BOARD)/jni

OUT = nohost.a


CFLAGS := -g -static
CFLAGS += \
	-D HASH_BLOOM=20 \
	-D CONFIG_ENABLE_MSG \
	-D CONFIG_ENABLE_DEBUG \
	-D CONFIG_DEVICE_TYPE_USER_RAMDRIVE \
	-D USER_MODE \
	-D USE_PMU \
	-D USE_KTIMER \
	-D USE_NEW_RMW \
	#-D ZYNQ=1 \ # Already defined in ConnectalProjectConfig.h:20 

INCLUDES := \
	-I$(PWD) \
	-I$(PWD)/$(INCLUDE) \
	-I$(PWD)/$(COMMON)/utils \
	-I$(PWD)/$(COMMON)/3rd \
	-I$(PWD)/$(DM_COMMON) \
	-I$(PWD)/connectal \
	-I$(PWD)/connectal/cpp \
	-I$(PWD)/$(BOARDDIR) \
	-I$(PWD)/connectal/drivers/zynqportal \
	-I$(PWD)/connectal/drivers/portalmem \


LIBSRC := \
	$(DM_COMMON)/dev_main.c \
	$(DM_COMMON)/dev_params.c \
	$(PWD)/$(BOARDDIR)/FlashRequest.c \
	$(PWD)/$(BOARDDIR)/FlashIndication.c \
	$(PWD)/$(BOARDDIR)/GeneratedCppCallbacks.cpp \
	$(PWD)/$(BOARDDIR)/MMURequest.c \
	$(PWD)/$(BOARDDIR)/MMUIndication.c \
	$(PWD)/$(BOARDDIR)/MemServerRequest.c \
	$(PWD)/$(BOARDDIR)/MemServerIndication.c \
	$(PWD)/connectal/cpp/portal.c \
	$(PWD)/connectal/cpp/portalPrintf.c \
	$(PWD)/connectal/cpp/transportHardware.c \
	$(PWD)/connectal/cpp/poller.cpp \
	$(PWD)/connectal/cpp/DmaBuffer.cpp \
	$(PWD)/connectal/cpp/dmaManager.c \
	$(PWD)/connectal/cpp/platformMemory.cpp \
	$(PWD)/connectal/cpp/timer.c \
	dm_nohost.cpp \

LIBOBJ := \
	$(DM_COMMON)/dev_main.o \
	$(DM_COMMON)/dev_params.o \
	$(PWD)/$(BOARDDIR)/FlashRequest.o \
	$(PWD)/$(BOARDDIR)/FlashIndication.o \
	$(PWD)/$(BOARDDIR)/GeneratedCppCallbacks.o \
	$(PWD)/$(BOARDDIR)/MMURequest.o \
	$(PWD)/$(BOARDDIR)/MMUIndication.o \
	$(PWD)/$(BOARDDIR)/MemServerRequest.o \
	$(PWD)/$(BOARDDIR)/MemServerIndication.o \
	$(PWD)/connectal/cpp/portal.o \
	$(PWD)/connectal/cpp/portalPrintf.o \
	$(PWD)/connectal/cpp/transportHardware.o \
	$(PWD)/connectal/cpp/poller.o \
	$(PWD)/connectal/cpp/DmaBuffer.o \
	$(PWD)/connectal/cpp/dmaManager.o \
	$(PWD)/connectal/cpp/platformMemory.o \
	$(PWD)/connectal/cpp/timer.o \
	dm_nohost.o \

all: $(OUT)

$(OUT): $(LIBOBJ)
	$(AR) r $(OUT) $(LIBOBJ)

.c.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

.cpp.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

clean:
	@$(RM) *.o core *~ nohost.a
	@$(RM) $(DM_COMMON)/*.o
	@$(RM) $(PWD)/$(BOARDDIR)/*.o
	@$(RM) $(PWD)/connectal/cpp/*.o

