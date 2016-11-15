echo "umount start!"
sudo umount /media/robusta
sleep 1
sudo rmmod robusta_drv
sleep 1
sudo rmmod risa_dev_ramdrive_timing
echo "umount done!"

