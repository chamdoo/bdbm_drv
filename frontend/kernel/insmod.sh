if [ $# -eq 0 ]; then echo " [ERROR] # of volumes should be input. ex "$0" 4"
else
	num=0;
	while (( $num < $1)); do
		sudo insmod bdbm_drv$num.ko _param_dev_num=$num
		let num+=1
	done
fi


