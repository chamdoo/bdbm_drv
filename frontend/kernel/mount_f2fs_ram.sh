sudo mkdir -p /usr/share/bdbm_drv
sudo touch /usr/share/bdbm_drv/ftl.dat
sudo touch /usr/share/bdbm_drv/dm.dat

sudo umount /media/robusta

sudo insmod risa_dev_ramdrive_timing.ko
sudo insmod robusta_drv.ko
sleep 1
sudo ~/Desktop/f2fs-tools-1.6.1/mkfs/mkfs.f2fs /dev/robusta
sudo mount -t f2fs -o discard /dev/robusta /media/robusta
