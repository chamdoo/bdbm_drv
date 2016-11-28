cd ../../devices/ramdrive/
make
cd ../../frontend/kernel
make clean
make
cp ../../devices/ramdrive/risa_dev_ramdrive.ko ./

sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
sudo sh -c "echo "" > /var/log/kern.log"
sudo mkdir -p /usr/share/bdbm_drv
sudo touch /usr/share/bdbm_drv/ftl.dat
sudo touch /usr/share/bdbm_drv/dm.dat

sudo insmod risa_dev_ramdrive.ko
sudo insmod robusta_drv.ko
sudo mkfs -t ext4 -b 4096 /dev/robusta
sudo mount -t ext4 -o discard /dev/robusta /media/robusta

sudo /etc/init.d/mysql stop
sudo rsync -avzh /var/lib/mysql /media/robusta/
sudo mount -B /media/robusta/mysql /var/lib/mysql
sudo /etc/init.d/mysql start

#cd ycsb
#cd tpcc
#. start_all.sh
#. start_load.sh
