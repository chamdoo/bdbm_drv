# Makefile for a RISA Device Driver
#
CC = arm-linux-gnueabihf-g++
AR = arm-linux-gnueabihf-ar

ROOTDIR = ../..
INCLUDE = $(ROOTDIR)/include
COMMON = $(ROOTDIR)/common
DM_COMMON = $(ROOTDIR)/devices/common
DM_NOHOST = $(ROOTDIR)/devices/nohost
BOARD = zc706_ubuntu
BOARDDIR = $(BOARD)/jni

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
	#-D USE_ACP \ # only valid for zc706. mkTop8k_ACP.exe should be used.
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

LIBS := \
	-lm -lpthread -lrt

SRCS := \
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
	$(COMMON)/utils/umemory.c \
	$(COMMON)/utils/uthread.c \
	$(DM_COMMON)/dev_main.c \
	$(DM_COMMON)/dev_params.c \
	$(DM_NOHOST)/dm_nohost.cpp \
	$(PWD)/libmemio.cpp \

OBJS := \
	$(SRCS:.c=.o) $(SRCS:.cpp=.o)


all: nohost.exe

nohost.exe: $(SRCS) libmemio.a main.cpp
	$(CC) $(INCLUDES) $(CFLAGS) -o $@ main.cpp libmemio.a $(LIBS) 

libmemio.a: $(OBJS)
	$(AR) r $(@) $(OBJS)

.c.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

.cpp.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

clean:
	@$(RM) *.o core *~ libmemio.a nohost.exe
	@$(RM) $(DM_COMMON)/*.o
	@$(RM) $(DM_NOHOST)/*.o
	@$(RM) $(PWD)/$(BOARDDIR)/*.o
	@$(RM) $(PWD)/connectal/cpp/*.o
	@$(RM) $(COMMON)/utils/*.o
