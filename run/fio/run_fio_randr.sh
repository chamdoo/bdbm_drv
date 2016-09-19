#!/bin/bash

sudo fio --randrepeat=1 \
	--name=libaio\
	--filename=/media/hoon/TEMP/fio \
	--bs=4k \
	--iodepth=128 \
	--size=7000M \
	--readwrite=randread \
	--rwmixwrite=0 \
	--rwmixread=100 \
	--overwrite=1 \
	--numjobs=8 \
	--direct=0 \
	--buffered=1
