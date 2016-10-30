BENCH_HOME=/home/ejlee/benchmark

if [[ $# -lt 1 ]]; then
	echo "Usage: ./exec.sh iozoe"
	exit
fi

if [[ $1 == "iozone" ]]; then
	$BENCH_HOME/iozone3_420/src/current/iozone -R -l 1 -u 1 -r 4k -s 1g -I -F /media/robusta/d -i 0
fi

if [[ $1 == "fio" ]]; then
	$BENCH_HOME/fio/fio $BENCH_HOME/fio/examples/jesd219.fio
fi

if [[ $1 == "tpcc" ]]; then
	service mysql stop
	cp ~/mysql_bak/mysql* /media/robusta/
	service mysql start
	sleep 3
	$BENCH_HOME/tpcc_start -h127.0.0.1 -dtpcc1000 -uroot -p -w20 -c16 -r10 -l1200 > ~/tpcc-output-ps-55-bpool-256.log
fi	
		


