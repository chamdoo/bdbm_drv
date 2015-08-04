# Makefile for a RISA Device Driver
#
CC = gcc 
OUT = libdummy.a
#LIBS =  -lpthread

INCLUDES := \
	-I$(PWD)/../../include \
	-I$(PWD)/../../include/utils \
	-I$(PWD)/../cpp

CFLAGS := \
	-g \
	-static \

CFLAGS += \
	-D HASH_BLOOM=20 \
	-D CONFIG_ENABLE_MSG \
	-D CONFIG_ENABLE_DEBUG \
	-D USE_PMU \
	-D USE_KTIMER \
	-D USER_MODE

LIBSRC := \
	../cpp/dm_params.c \
	dm_dummy.c \
	#../../include/utils/utime.c \
	#../../include/utils/uthread.c \

LIBOBJ=$(LIBSRC:.c=.o)

$(OUT): $(LIBOBJ)
	$(AR) r $(OUT) $(LIBOBJ)

.c.o:
	$(CC) $(INCLUDES) $(LIBS) $(CFLAGS) -c $< -o $@

clean:
	@$(RM) *.o core *~ libdummy.a
