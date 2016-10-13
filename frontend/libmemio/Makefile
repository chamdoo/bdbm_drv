# Makefile for a RISA Device Driver
#
CC = arm-linux-gnueabihf-g++
ROOTDIR = ../..
INCLUDE = $(ROOTDIR)/include
COMMON = $(ROOTDIR)/common
DM_COMMON = $(ROOTDIR)/devices/common
DM_NOHOST = $(ROOTDIR)/devices/nohost
BOARDDIR = zc706_ubuntu/jni

CFLAGS += \
	-g \
	-Wall -Wsign-compare -Wwrite-strings \
	-D_LARGEFILE64_SOURCE \
	-D_GNU_SOURCE  \
	-D HASH_BLOOM=20 \
	-D CONFIG_ENABLE_MSG \
	-D CONFIG_ENABLE_DEBUG \
	-D CONFIG_DEVICE_TYPE_USER_RAMDRIVE \
	-D USER_MODE \
	-D USE_PMU \
	-D USE_KTIMER \
	-D USE_NEW_RMW \
	-D ZYNQ=1 \

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

LIBS := \
	-lm -lpthread -lrt

TARGETSRC := \
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
	$(PWD)/connectal/cpp/dmaManager.c \
	$(PWD)/connectal/cpp/platformMemory.cpp \
	$(PWD)/connectal/cpp/timer.c \
	$(COMMON)/utils/umemory.c \
	$(DM_COMMON)/dev_params.c \
	$(DM_NOHOST)/dm_nohost.cpp \
	$(DM_COMMON)/dev_main.c \

#TARGETOBJ := \
	$(DM_NOHOST)/dm_nohost.o \
	$(DM_COMMON)/dev_params.o \
	$(DM_COMMON)/dev_main.o \
	$(COMMON)/utils/umemory.o \
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
	$(PWD)/connectal/cpp/dmaManager.o \
	$(PWD)/connectal/cpp/platformMemory.o \
	$(PWD)/connectal/cpp/timer.o \

TARGETOBJ := $(TARGETSRC:.c=.o)
TARGETOBJ += $(TARGETSRC:.cpp=.o)


all: nohost.exe

nohost.exe: $(TARGETSRC) nohost.a main.cpp
	$(CC) $(INCLUDES) $(CFLAGS) -o $@ main.cpp nohost.a $(LIBS) 

nohost.a: $(TARGETOBJ)
	$(AR) r $(@) $(TARGETOBJ)

.c.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

.cpp.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

clean:
	@$(RM) *.o core *~ nohost.a nohost.exe
	@$(RM) $(DM_COMMON)/*.o
	@$(RM) $(DM_NOHOST)/*.o
	@$(RM) $(PWD)/$(BOARDDIR)/*.o
	@$(RM) $(PWD)/connectal/cpp/*.o
	@$(RM) $(COMMON)/utils/*.o

