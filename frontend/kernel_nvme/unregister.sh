sudo umount /media/blueDBM
sleep 1
sudo rmmod nvme
sleep 1
sudo rmmod bdbm_drv
sleep 1
sudo rmmod risa_dev_*
#sudo rmmod f2fs
