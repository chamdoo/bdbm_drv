#!/bin/bash

sudo fio --randrepeat=1 \
	--ioengine=libaio \
	--name=fio\
	--filename=/media/blueDBM/fio \
	--bs=4k \
	--blockalign=4k \
	--iodepth=128 \
	--size=1000M \
	--readwrite=randrw \
	--rwmixread=50 \
	--rwmixwrite=50 \
	--overwrite=1 \
	--direct=0

