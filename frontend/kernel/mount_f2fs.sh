sudo mkdir -p /usr/share/bdbm_drv
sudo touch /usr/share/bdbm_drv/ftl.dat
sudo touch /usr/share/bdbm_drv/dm.dat

sudo rmmod nvme
sudo insmod dumbssd.ko
sleep 1
sudo insmod nvme.ko
sleep 1
sudo insmod bdbm_drv.ko
sleep 1
sudo mkfs.f2fs -a 0 -s 2 /dev/robusta
sudo mount -t f2fs -o discard /dev/robusta /media/robusta
