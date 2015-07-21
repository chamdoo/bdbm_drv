# Makefile for a RISA Device Driver
#

CC = gcc
SRC := ../src
CFLAGS := -Wall -g -O2 -D_LARGEFILE64_SOURCE -D_GNU_SOURCE 
LIBS =  -lm
INCLUDES = -I$(PWD)/../../include -I$(PWD)/../../include/utils -I$(PWD)/../src

CFLAGS += -D HASH_BLOOM=20 \
		  -D CONFIG_ENABLE_MSG \
		  -D CONFIG_ENABLE_DEBUG \
		  -D USE_PMU \
		  -D USE_KTIMER \
		  -D USER_MODE

OBJS := \
	../../include/utils/utime.c \
	../../include/utils/ufile.c \
	$(SRC)/kparams.c \
	$(SRC)/ioctl.c \
	$(SRC)/kmain.c \
	$(SRC)/pmu.c \
	$(SRC)/host_block.c \
	$(SRC)/hlm_nobuf.c \
	$(SRC)/hlm_buf.c \
	$(SRC)/hlm_rsd.c \
	$(SRC)/llm_noq.c \
	$(SRC)/llm_mq.c \
	$(SRC)/algo/abm.c \
	$(SRC)/algo/no_ftl.c \
	$(SRC)/algo/block_ftl.c \
	$(SRC)/algo/page_ftl.c \
	$(SRC)/queue/queue.c \
	$(SRC)/queue/prior_queue.c

ftlib: $(OBJS)
	gcc -o $@ $^ $(INCLUDES) $(CFLAGS) $(LIBS)

clean:
	rm *.o core *~ ftlib
