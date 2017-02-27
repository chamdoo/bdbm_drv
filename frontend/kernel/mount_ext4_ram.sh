sudo mkdir -p /usr/share/bdbm_drv
sudo touch /usr/share/bdbm_drv/ftl.dat
sudo touch /usr/share/bdbm_drv/dm.dat

sudo insmod risa_dev_ramdrive.ko
sudo insmod robusta_drv.ko
sudo mkfs -t ext2 -b 4096 /dev/robusta
sudo mount -t ext2 /dev/robusta /media/robusta
#sudo mount -t ext2 -o discard /dev/robusta /media/robusta
