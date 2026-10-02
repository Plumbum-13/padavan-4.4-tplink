#!/bin/sh

### Blocking reclama

func_start()
{
if [ -f "/tmp/block.hosts" ]; then
	logger -t adblock "AdBlock is running."
else
	if [ -f "/usr/hosts" ]; then 
		logger -t adblock "Entware hosts file." && cp /usr/hosts /tmp/block.hosts
	fi
	touch /tmp/block.hosts
fi
}


func_stop()
{
if [ -f "/tmp/block.hosts" ]; then
	logger -t adblock "AdBlock disable."
	rm /tmp/block.hosts
else
	logger -t adblock "No AdBlock enable."
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
	echo "Usage: $0 {start|stop|restart}"
	exit 1
	;;
esac

exit 0
