cd /home/ejlee/cooperate/bdbm_drv_dummy_gc/devices/ramdrive_timing; make clean; make
cd /home/ejlee/cooperate/bdbm_drv_dummy_gc/frontend/kernel; make clean; make
/home/ejlee/cooperate/bdbm_drv_dummy_gc/frontend/kernel/mount_ext4_ram.sh
df -h
cat /home/ejlee/cooperate/bdbm_drv_dummy_gc/include/bdbm_drv.h | grep MAX_QSIZE

