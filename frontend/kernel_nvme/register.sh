sudo mkdir -p /usr/share/bdbm_drv
sudo touch /usr/share/bdbm_drv/ftl.dat
sudo touch /usr/share/bdbm_drv/dm.dat

sudo insmod risa_dev_ramdrive.ko
sudo insmod bdbm_drv.ko
sudo rmmod nvme
sudo insmod ../../../kernel_4.5.3/nvme/nvme.ko
#sudo mkfs -t ext4 -b 4096 /dev/nvme0n1
#sudo mount \-t ext4 \-o discard /dev/nvme0n1 /media/blueDBM
