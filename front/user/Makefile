# Makefile for a RISA Device Driver
#

CC = gcc
FTL := ../../ftl
COMMON := ../../include
CFLAGS := -Wall -g -O2 -D_LARGEFILE64_SOURCE -D_GNU_SOURCE 
LIBS =  -lm -lpthread
INCLUDES = -I$(PWD)/../../include -I$(PWD)/../../include/utils -I$(PWD)/$(FTL)

CFLAGS += -D HASH_BLOOM=20 \
		  -D CONFIG_ENABLE_MSG \
		  -D CONFIG_ENABLE_DEBUG \
		  -D USE_PMU \
		  -D USE_KTIMER \
		  -D USER_MODE

OBJS := \
	main.c \
	uparams.c \
	host_user.c \
	dm_user.c \
	dm_params.c \
	$(FTL)/pmu.c \
	$(FTL)/hlm_nobuf.c \
	$(FTL)/hlm_buf.c \
	$(FTL)/hlm_rsd.c \
	$(FTL)/llm_noq.c \
	$(FTL)/llm_mq.c \
	$(FTL)/algo/abm.c \
	$(FTL)/algo/no_ftl.c \
	$(FTL)/algo/block_ftl.c \
	$(FTL)/algo/page_ftl.c \
	$(FTL)/queue/queue.c \
	$(FTL)/queue/prior_queue.c \
	$(COMMON)/utils/utime.c \
	$(COMMON)/utils/ufile.c \
	$(COMMON)/utils/uthread.c \
	$(COMMON)/3rd/uilog.c \

	#$(FTL)/host_block.c \
	#$(FTL)/kparams.c \
	#$(FTL)/ioctl.c \
	#$(FTL)/kmain.c \

ftlib: $(OBJS)
	$(CC) -o $@ $^ $(INCLUDES) $(CFLAGS) $(LIBS)

clean:
	@$(RM) *.o core *~ ftlib
	@cd ../../ftl; rm -rf *.o .*.cmd; rm -rf */*.o */.*.cmd;
