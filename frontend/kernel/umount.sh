if [ $# -eq 0 ]; then echo " [ERROR] # of volumes should be input. ex "$0" 4"
else
	num=0;
	while (( $num < $1));do
		sudo umount /mnt/test$num
		sudo rmmod bdbm_drv$num
		let num+=1
	done
	sudo rmmod risa_dev_ramdrive
fi

