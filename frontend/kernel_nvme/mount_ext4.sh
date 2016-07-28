#sudo mkfs -t ext4 -b 4096 /dev/nvme0n1
#sudo mount \-t ext4 \-o discard /dev/nvme0n1 /media/blueDBM

sudo mkfs -t ext4 -b 4096 /dev/blueDBM
sudo mount \-t ext4 \-o discard /dev/blueDBM /media/blueDBM
