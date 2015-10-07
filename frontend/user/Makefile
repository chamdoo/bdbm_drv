# Makefile for a RISA Device Driver
#

CC = gcc
FTL := ../../ftl
INCLUDE := ../../include
LIB := ../../lib

CFLAGS := -Wall -g -D_LARGEFILE64_SOURCE -D_GNU_SOURCE 
LIBS += -lm -lpthread -lrt

INCLUDES = -I$(PWD)/$(INCLUDE) \
		   -I$(PWD)/$(LIB)/utils \
		   -I$(PWD)/$(LIB)/3rd \
		   -I$(PWD)/$(FTL)

CFLAGS += -D HASH_BLOOM=20 \
		  -D CONFIG_ENABLE_MSG \
		  -D CONFIG_ENABLE_DEBUG \
		  -D USER_MODE \
		  -D USE_PMU \

#DMLIB := ../../devices/libdummy/libdummy.a
DMLIB := ../../devices/libramdrive/libramdrive.a
#DMLIB := dm_proxy.c

SRCS := \
	main.c \
	uparams.c \
	host_user.c \
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
	$(LIB)/utils/utime.c \
	$(LIB)/utils/ufile.c \
	$(LIB)/utils/uthread.c \
	$(LIB)/utils/upage.c \
	$(LIB)/3rd/uilog.c \
	$(DMLIB) \


libftl: $(SRCS) $(DMLIB)
	$(CC) $(INCLUDES) $(CFLAGS) -o $@ $(SRCS) $(LIBS) 

clean:
	@$(RM) *.o core *~ libftl 
	@cd ../../ftl; rm -rf *.o .*.cmd; rm -rf */*.o */.*.cmd;
	@cd ../../include; rm -rf *.o .*.cmd; rm -rf */*.o */.*.cmd;
	@cd ../../include/utils; rm -rf *.o .*.cmd; rm -rf */*.o */.*.cmd;

