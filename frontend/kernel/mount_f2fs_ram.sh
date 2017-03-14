sudo mkdir -p /usr/share/bdbm_drv
sudo touch /usr/share/bdbm_drv/ftl.dat
sudo touch /usr/share/bdbm_drv/dm.dat

sudo insmod risa_dev_ramdrive.ko
sudo insmod robusta_drv.ko
sudo mkfs.f2fs -a 0 /dev/robusta
sudo mount -t f2fs -o discard /dev/robusta /media/robusta
