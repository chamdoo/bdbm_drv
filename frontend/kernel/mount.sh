if [ $# -eq 0 ]; then echo " [ERROR] # of volumes should be input. ex "$0" 4"
else
	num=0;
	while (( $num < $1));do
		sudo mkfs -t ext4 -b 4096 /dev/blueDBM$num
		sudo mount /dev/blueDBM$num /mnt/test$num
		let num+=1
	done
fi

