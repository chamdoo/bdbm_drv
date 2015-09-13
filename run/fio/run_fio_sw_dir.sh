#!/bin/bash

sudo fio --randrepeat=1 \
	--ioengine=libaio \
	--name=fio\
	--filename=/media/blueDBM/fio \
	--bs=4k \
	--iodepth=128 \
	--size=1000M \
	--readwrite=write \
	--rwmixread=100 \
	--overwrite=1 \
	--direct=1

