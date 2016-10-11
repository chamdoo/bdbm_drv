sudo mkdir -p /usr/share/bdbm_drv
sudo touch /usr/share/bdbm_drv/ftl.dat
sudo touch /usr/share/bdbm_drv/dm.dat

sudo umount /media/robusta

sudo rmmod robusta_drv
sudo rmmod risa_dev_ramdrive

sudo insmod risa_dev_ramdrive.ko
sudo insmod robusta_drv.ko
sudo mkfs -t ext4 -b 4096 /dev/robusta
sudo mount -t ext4 -o discard /dev/robusta /media/robusta
