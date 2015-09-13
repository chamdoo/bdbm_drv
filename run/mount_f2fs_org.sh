sudo mkdir -p /usr/share/bdbm_drv
sudo touch /usr/share/bdbm_drv/ftl.dat
sudo touch /usr/share/bdbm_drv/dm.dat

sudo insmod risa_dev_ramdrive.ko
sudo insmod bdbm_drv.ko
#sudo insmod f2fs.org.ko
sudo insmod f2fs.ko
sudo ./bdbm_format /dev/blueDBM
sudo mkfs.f2fs.org -a 0 -s 2 /dev/blueDBM
sudo mount \-t f2fs \-o discard /dev/blueDBM /media/blueDBM
