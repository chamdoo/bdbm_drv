SIZE="n500m n1g n2g"

echo "nvm_size trim io(MB) bw(KB/s) iops runt(msec)"


for s in $SIZE;do
	f="fio_toff_o1_$s.rslt"
	echo $s off `grep "runt" $f  | grep "read" |sed -e 's/[a-z, A-Z,\/,\:]*//g' | sed -e 's/=/ /g'` 

done

echo " "

for s in $SIZE;do
	f="fio_ton_o1_$s.rslt"
	echo $s on `grep "runt" $f  | grep "read" |sed -e 's/[a-z, A-Z,\/,\:]*//g' | sed -e 's/=/ /g'` 
done
