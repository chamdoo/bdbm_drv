#!/bin/bash

#sudo fio --randrepeat=1 \
	#--ioengine=libaio \
	#--buffered=1 \
	#--name=fio\
	#--filename=/media/blueDBM/fio \
	#--bs=4k \
	#--iodepth=128 \
	#--size=1000M \
	#--readwrite=randrw \
	#--rwmixread=0 \
	#--overwrite=1 \
	#--numjobs=8 \
	#--fsync=1000

sudo rm /media/robusta/fio

sudo fio --randrepeat=1 \
	--ioengine=libaio \
	--name=fio\
	--filename=/media/robusta/fio \
	--bs=4k-16k \
	--iodepth=64 \
	--size=500M \
	--readwrite=randrw \
	--rwmixread=0 \
	--overwrite=0 \
	--numjobs=8 \
	--direct=0 \
	--buffered=0

