# Makefile for a RISA Device Driver
#

CC = gcc
FTL := ../../ftl
INCLUDE := ../../include
COMMON := ../../common
DM_COMMON := ../../devices/common

CFLAGS := -Wall -g -D_LARGEFILE64_SOURCE -D_GNU_SOURCE 
LIBS += -lm -lpthread -lrt

INCLUDES = -I$(PWD)/$(INCLUDE) \
		   -I$(PWD)/$(COMMON)/utils \
		   -I$(PWD)/$(COMMON)/3rd \
		   -I$(PWD)/$(FTL) \
		   -I$(PWD)/$(DM_COMMON) \

CFLAGS += -D HASH_BLOOM=20 \
		  -D CONFIG_ENABLE_MSG \
		  -D CONFIG_ENABLE_DEBUG \
		  -D USER_MODE \
		  -D USE_PMU \

#DMCOMMON := ../../devices/libdummy/libdummy.a
DMCOMMON := ../../devices/libramdrive/libramdrive.a
#DMCOMMON := dm_proxy.c

SRCS := \
	main.c \
	host_user.c \
	$(FTL)/ftl_params.c \
	$(FTL)/pmu.c \
	$(FTL)/hlm_nobuf.c \
	$(FTL)/hlm_buf.c \
	$(FTL)/hlm_dftl.c \
	$(FTL)/hlm_rsd.c \
	$(FTL)/llm_noq.c \
	$(FTL)/llm_mq.c \
	$(FTL)/algo/abm.c \
	$(FTL)/algo/no_ftl.c \
	$(FTL)/algo/block_ftl.c \
	$(FTL)/algo/page_ftl.c \
	$(FTL)/algo/dftl_map.c \
	$(FTL)/algo/dftl.c \
	$(FTL)/queue/queue.c \
	$(FTL)/queue/prior_queue.c \
	$(FTL)/queue/rd_prior_queue.c \
	$(COMMON)/utils/utime.c \
	$(COMMON)/utils/ufile.c \
	$(COMMON)/utils/uthread.c \
	$(COMMON)/utils/upage.c \
	$(COMMON)/3rd/uilog.c \
	$(COMMON)/bdbm_main.c \
	$(DMCOMMON) \


libftl: $(SRCS) $(DMCOMMON)
	$(CC) $(INCLUDES) $(CFLAGS) -o $@ $(SRCS) $(LIBS) 

clean:
	@$(RM) *.o core *~ libftl 
	@cd ../../ftl; rm -rf *.o .*.cmd; rm -rf */*.o */.*.cmd;
	@cd ../../include; rm -rf *.o .*.cmd; rm -rf */*.o */.*.cmd;
	@cd ../../include/utils; rm -rf *.o .*.cmd; rm -rf */*.o */.*.cmd;

