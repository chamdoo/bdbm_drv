#!/bin/bash

sudo rm /media/robusta/fio

sudo fio --randrepeat=1 \
	--ioengine=libaio \
	--name=fio\
	--filename=/media/robusta/fio \
	--bs=4k \
	--iodepth=128 \
	--size=200M \
	--readwrite=randrw \
	--rwmixread=0 \
	--overwrite=1 \
	--numjobs=1 \
	--direct=0 \
	--buffered=0

