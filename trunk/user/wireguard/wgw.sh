#!/bin/sh

IFACE="wgw"             # warp iface name
WG_IP="172.16.0.2"      # local addr for tunnel
WG_MASK="32"            # only one
WG_CONFIG="/etc/storage/warp/wgw.conf"  # config
RESOLVER="1.0.0.1"      # warp dns
MTU="1280"              # from warp for Android
UNBLOCK="/etc/storage/warp/unblock_wgw.txt" # путь к файлу с разблокируемыми ресурсами
WARP_ROUTES="/tmp/warp.ip"
WARP_HOSTS="/tmp/warp.hosts"

IP_OCT="[0-9]{1,3}"
IP_HOST=$(echo "$IP_OCT\.$IP_OCT\.$IP_OCT\.$IP_OCT")
IP_CIDR=$(echo "$IP_HOST/[0-9]{1,2}")


get_ip4(){

  site="$1"
  lookup=$( nslookup "$site" "$RESOLVER" 2>&1 )
  error=$( echo "$?" )
  ip4=$( echo "$lookup" | tail -n +4 | grep -Eo "$IP_HOST" )    # assuming nslookup doesn't produce CIDR

    if [ "$error" -eq "0" ] ;
    then
      echo -e "$ip4"
    else
      logger -t warp "Cannot get ip for [$site]"
      exit 1
    fi
}

warp_parse_file(){
# generate ip addresses list to be tunneled via warp

  cat /dev/null > $WARP_ROUTES
  cat /dev/null > $WARP_HOSTS

  while read line ;
  do

    if [ -n "$line" ] && [ "${line:0:1}" != "#" ] ;
    then

      if [ -n "$(echo $line | grep -Eo -e $IP_HOST -e $IP_CIDR)" ] ;
      then
        # already got ip
        ip=$line

      else
        #need to call dns
        ip=$( get_ip4 "$line" )

          if [ "$?" -eq "0" ] ;
          then
            ip_host=$( echo "$ip" | sed "s/.*/& $line/" )
            echo "$ip_host" >> $WARP_HOSTS
          fi
      fi

    echo "$ip" >> $WARP_ROUTES

    fi

  done < $UNBLOCK

}


warp_add_routes(){

  count=0

  while read line ;
  do

    if [ -n "$line" ] && [ "${line:0:1}" != "#" ] ;
    then

      errline=$( ip route add "$line" via "$WG_IP" 2>&1 )
      [[ "$?" -eq "0" ]] && count=$((count+1)) || logger -t warp "Cannot add route for $line. $errline"

    fi

  done < $WARP_ROUTES

  logger -t warp "Added $count new route(s) from $WARP_ROUTES"

}



start(){

if [ -f "/var/run/wgw.pid" ]; then
  logger -t warp "WARP services is running."
else

  modprobe -s wireguard
  ip link add dev $IFACE type wireguard
  ip addr add $WG_IP/$WG_MASK dev $IFACE
  wg setconf $IFACE $WG_CONFIG
  ip link set $IFACE up
  [[ "$?" -eq "0" ]] && logger -t warp "WARP tunnel is up on $WG_IP"
  [[ -n "$MTU" ]] && ip link set mtu $MTU up dev $IFACE

  iptables -I INPUT -i $IFACE -j ACCEPT

  iptables -t nat -A POSTROUTING -o wgw -j MASQUERADE 

  # add resolver
  ip route add $RESOLVER via $WG_IP
  [[ "$?" -eq "0" ]] && logger -t warp "Resolver $RESOLVER routed via tunnel"
  touch /var/run/wgw.pid
fi
}


stop(){

if [ -f "/var/run/wgw.pid" ]; then
  ip link set $IFACE down
  ip link delete dev $IFACE
  logger -t warp "WARP tunnel at $WG_IP was removed"
  logger -t warp "Shutdown."
  rm /var/run/wgw.pid
else
  logger -t warp "WARP services is stoping."
fi
}



update(){

  ping -c 1 -I $IFACE $RESOLVER | grep -Eo "ttl=$IP_OCT" > /dev/null
  # do we got ttl?
  [[ "$?" -eq "0" ]] && warp_parse_file ; warp_add_routes  || logger -t warp "Cannot update routes: WARP tunnel seems not working"

  restart_dhcpd         # for hosts update

}



case "$1" in
start)
        start
        update
        ;;
stop)
        stop
        ;;
restart)
        stop
        start
        update
        ;;
update)
        update
        ;;
*)
        echo "Usage: $0 {start|stop|restart|update}"

        exit 1
        ;;
esac
