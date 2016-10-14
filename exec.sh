BENCH_HOME=/home/ejlee/benchmark

if [[ $# -lt 1 ]]; then
	echo "Usage: ./exec.sh iozoe"
	exit
fi

if [[ $1 == "iozone" ]]; then
	$BENCH_HOME/iozone3_420/src/current/iozone -R -l 1 -u 1 -r 4k -s 1g -I -F /media/robusta/d -i 0 -i 2
fi


