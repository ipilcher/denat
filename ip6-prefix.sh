#!/bin/sh

[ "$interface" = bond0.256 ] || exit
[ "$old_ip6_prefix" = "$new_ip6_prefix" ] && exit


#
# The prefix has changeid (somehow), so clear the iptables chain, remove
# the old route (if possible/applicable), and write the state file.
#

echo "$new_ip6_prefix" > /run/dhclient6-prefix.new
mv /run/dhclient6-prefix.new /run/dhclient6-prefix

/usr/sbin/ip6tables -F FWD-INET6-IN

if [ -n "$old_ip6_prefix" ] ; then
    if [ -n "$(/usr/sbin/ip -6 route show $old_ip6_prefix)" ] ; then
	/usr/sbin/ip -6 route del $old_ip6_prefix
    fi
fi


#
# If a new prefix was passed, add the iptables rules and route (if
# the latter doesn't already exist).
#

if [ -n "$new_ip6_prefix" ] ; then

    ASTERISK_ADDR=${new_ip6_prefix%00::/56}ff::2/128
    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p tcp -m tcp --dport 80 -j ACCEPT
    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p tcp -m tcp --dport 443 -j ACCEPT
    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p tcp -m tcp --dport 32698 -j ACCEPT
    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p tcp -m tcp --dport 32789:32790 -j ACCEPT

    if [ -z "$(/usr/sbin/ip -6 route show $new_ip6_prefix)" ] ; then
	/usr/sbin/ip -6 route add $new_ip6_prefix via fd00:dcaf:bad:ff::1
    fi
fi
