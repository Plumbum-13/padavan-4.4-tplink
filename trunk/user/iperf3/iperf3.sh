#!/bin/sh

func_start(){
if [ -f "/var/run/iperf3.pid" ]; then
	logger -t iperf3 "iPerf3 is running."
else
	iperf3 -s -D
	logger -t iperf3 "iPerf3 server is running."
	touch /var/run/iperf3.pid
fi
}

func_stop(){
if [ -f "/var/run/iperf3.pid" ]; then
	killall -q iperf3
	logger -t iperf3 "iPerf3 server is stoping."
else
	logger -t iPerf3 "iPerf3 not starting."
fi
}

case "$1" in
start)
	func_start
	;;
stop)
	func_stop
	;;
restart)
	func_stop
	func_start
	;;
*)
	echo "Usage: $0 { start | stop | restart }"
	exit 1
	;;
esac
