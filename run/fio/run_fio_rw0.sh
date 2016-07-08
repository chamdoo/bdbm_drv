#!/bin/bash

sudo fio --randrepeat=1 \
	--ioengine=libaio \
	--name=fio\
	--filename=/mnt/test0/fio \
	--bs=4k \
	--iodepth=128 \
	--size=300M \
	--readwrite=randrw \
	--rwmixread=0 \
	--overwrite=0 \
	--numjobs=8 \
	--direct=1 \
	--buffered=0

