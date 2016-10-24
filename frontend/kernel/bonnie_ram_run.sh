#!/bin/bash

# for 24 GB
sudo rm /media/robusta/1 -rf
sudo rm /media/robusta/2 -rf
sudo rm /media/robusta/3 -rf
sudo mkdir /media/robusta/1
sudo bonnie++ -s 1280:1024 -n 40 -x 4 -r 8 -u root -d /media/robusta/1 &
sudo mkdir /media/robusta/2
sudo bonnie++ -s 1280:1024 -n 40 -x 4 -r 8 -u root -d /media/robusta/2 &
sudo mkdir /media/robusta/3
sudo bonnie++ -s 1280:1024 -n 40 -x 4 -r 8 -u root -d /media/robusta/3 &

# for 16 GB
#sudo mkdir /media/robusta/1
#sudo bonnie++ -s 2500:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/robusta/1 &
#sudo mkdir /media/robusta/2
#sudo bonnie++ -s 2500:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/robusta/2 &
#sudo mkdir /media/robusta/3
#sudo bonnie++ -s 2500:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/robusta/3 &
#sudo mkdir /media/robusta/4
#sudo bonnie++ -s 2500:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/robusta/4 &

for job in `jobs -p`
do
	echo $job
   	wait $job
done
