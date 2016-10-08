BENCH_HOME=$HOME/benchmark

if [[ $# -lt 1 ]]; then
	echo "Usage: ./exec.sh iozone"
	exit
fi

if [[ $1 == "iozone" ]]; then
#	$BENCH_HOME/iozone3_420/src/current/iozone -R -l 1 -u 1 -r 4k -s 100m -I -F /media/robusta/d -i 0 -i 2
	$BENCH_HOME/iozone3_420/src/current/iozone -R -l 1 -u 1 -r 4k -s 5g -I -F /media/robusta/d -i 0 -i 2
fi
if [[ $1 == "fio" ]]; then
	echo "fio..."
	FHOME=$BENCH_HOME/fio
	$FHOME/fio $FHOME/examples/jesd219.fio
fi
