#!/bin/bash

# umount the file-system
sudo umount /media/blueDBM

# sto the libftl
echo 'kill sudo ./libftl'
libftl_pid=`ps -ef | grep "sudo [.]/libftl" | awk '{print $2}'`
sudo kill -2 $libftl_pid
sleep 1

# rm the device
sudo rmmod bdbm_drv
sudo rmmod risa_dev_*
sudo rmmod f2fs
