CFLAGS=-g -Wall 
CC=gcc 
LIBS=-lm -lpthread 
INCLUDES= 
OBJS_FORMAT=dumbssd-test.o 
SRCS_FORMAT=dumbssd-test.c 

#The variable $@ has the value of the target. In this case $@ = psort 
dumbssd-test: ${OBJS_FORMAT} 
		${CC} ${CFLAGS} ${INCLUDES} -o $@ ${OBJS_FORMAT} ${LIBS} 

.c.o: 
		${CC} ${CFLAGS} ${INCLUDES} -c $< 

clean: 
		rm *.o dumbssd-test
