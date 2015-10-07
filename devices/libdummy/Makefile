# Makefile for a RISA Device Driver
#
CC = gcc 
OUT = libdummy.a
INCLUDE = ../../include
LIB = ../../lib
COMMON = ../common

INCLUDES := \
	-I$(PWD)/$(INCLUDE) \
	-I$(PWD)/$(LIB)/utils \
	-I$(PWD)/$(LIB)/3rd \
	-I$(PWD)/$(COMMON)

CFLAGS := \
	-g \
	-static \

CFLAGS += \
	-D HASH_BLOOM=20 \
	-D CONFIG_ENABLE_MSG \
	-D CONFIG_ENABLE_DEBUG \
	-D CONFIG_DEVICE_TYPE_USER_DUMMY \
	-D USE_PMU \
	-D USE_KTIMER \
	-D USER_MODE

LIBSRC := \
	dm_dummy.c \
	$(COMMON)/dm_main.c \
	$(COMMON)/dm_params.c \

LIBOBJ=$(LIBSRC:.c=.o)

$(OUT): $(LIBOBJ) $(LIBSRC)
	$(AR) r $(OUT) $(LIBOBJ)

.c.o:
	$(CC) $(INCLUDES) $(LIBS) $(CFLAGS) -c $< -o $@

clean:
	@$(RM) *.o core *~ libdummy.a
	@cd $(COMMON); $(RM) *.o
