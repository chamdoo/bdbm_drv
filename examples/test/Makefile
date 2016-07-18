CFLAGS=-g -Wall 
CC=gcc 
LIBS=-lm -lpthread 
INCLUDES= 
OBJS_FORMAT=test.o 
SRCS_FORMAT=test.c 

#The variable $@ has the value of the target. In this case $@ = psort 
test: ${OBJS_FORMAT} 
		${CC} ${CFLAGS} ${INCLUDES} -o $@ ${OBJS_FORMAT} ${LIBS} 

.c.o: 
		${CC} ${CFLAGS} ${INCLUDES} -c $< 

clean: 
		rm *.o test 
