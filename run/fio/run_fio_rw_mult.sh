#!/bin/bash

if [ $# -eq 0 ]; then echo " [ERROR] # of volumes should be input. ex "$0" 4"
else
	num=0;
	while (( $num < $1)); do
		sudo fio --randrepeat=1 \
			--ioengine=libaio \
			--name=fio\
			--filename=/mnt/test$num/fio \
			--bs=4k \
			--iodepth=128 \
			--size=100M \
			--readwrite=randrw \
			--rwmixread=0 \
			--overwrite=0 \
			--numjobs=8 \
			--direct=1 \
			--buffered=0
		let num+=1
	done
fi

