#!/bin/bash

sudo rm /media/robusta/fio

sudo fio --randrepeat=1 \
	--ioengine=libaio \
	--name=fio\
	--filename=/media/robusta/fio \
	--bs=4k \
	--iodepth=1 \
	--size=1000M \
	--readwrite=write \
	--rwmixread=0 \
	--overwrite=0 \
	--numjobs=1 \
	--direct=0 \
	--buffered=0

