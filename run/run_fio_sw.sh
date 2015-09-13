#!/bin/bash

sudo fio --randrepeat=1 \
	--ioengine=libaio \
	--buffered=1 \
	--name=fio\
	--filename=/media/blueDBM/fio \
	--bs=4k \
	--iodepth=128 \
	--size=1000M \
	--readwrite=write \
	--rwmixread=100 \
	--overwrite=1 \
	--numjobs=8

