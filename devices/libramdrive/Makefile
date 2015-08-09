# Makefile for a RISA Device Driver
#
CC = gcc 
OUT = libramdrive.a
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
	-D CONFIG_DEVICE_TYPE_USER_RAMDRIVE \
	-D USE_PMU \
	-D USE_KTIMER \
	-D USER_MODE

#LIBSRC := \
	../../include/utils/ufile.c \
	../../include/utils/utime.c \
	../cpp/dm_main.c \
	../cpp/dm_params.c \
	../ramdrive/dev_ramssd.c \
	../ramdrive/dm_ramdrive.c \

LIBSRC := \
	../cpp/dm_main.c \
	../cpp/dm_params.c \
	../ramdrive/dev_ramssd.c \
	../ramdrive/dm_ramdrive.c \

LIBOBJ=$(LIBSRC:.c=.o)

$(OUT): $(LIBOBJ)
	$(AR) r $(OUT) $(LIBOBJ)

.c.o:
	$(CC) $(INCLUDES) $(LIBS) $(CFLAGS) -c $< -o $@

clean:
	@$(RM) *.o core *~ libramdrive.a
	@cd ../cpp; $(RM) *.o
	@cd ../ramdrive; $(RM) *.o
