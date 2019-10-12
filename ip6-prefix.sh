padword() {
    local WORD=$1
    while [ $(expr length "$WORD") -lt 4 ] ; do
	WORD=0${WORD}
    done
    echo $WORD
}

v6merge() {

    local PREFIX=$1
    local HOST=$2

    PREFIXLEN=${PREFIX#*/}

    if [ $PREFIXLEN -gt 56 -o $PREFIXLEN -lt 48 ] ; then
	/usr/bin/logger "ip6_prefix: ignoring invalid prefix: $PREFIX"
	return 1
    fi

    if [ $((PREFIXLEN%4)) != 0 ] ; then
	/usr/bin/logger "ip6_prefix: ignoring invalid prefix: $PREFIX"
	return 1
    fi

    local W1=${PREFIX%%:*}
    W1=$(padword $W1)

    local W2=${PREFIX#*:}
    W2=${W2%%:*}
    W2=$(padword $W2)

    local W3=${PREFIX#*:*:}
    W3=${W3%%:*}
    W3=$(padword $W3)

    local W4=${PREFIX#*:*:*:}
    W4=${W4%%:*}
    W4=$(padword $W4)
    W4=${W4%??}

    echo "${W1}:${W2}:${W3}:${W4}${HOST}/128"
}

ip6_prefix() {

    [ "$interface" = bond0.256 ] || return
    [ -z "$old_ip6_prefix" -a -z "$new_ip6_prefix" ] && return

    # We always write a new state file (which will contain only a blank line if
    # we don't have prefix).
    echo "$new_ip6_prefix" > /run/dhclient6-prefix.new
    mv /run/dhclient6-prefix.new /run/dhclient6-prefix

    # Flush the iptables rules; we'll (re-)create them if we have a valid new
    # prefix.  (*New* incoming connections will be blocked in the interim.)
    /usr/sbin/ip6tables -F FWD-INET6-IN

    # Do we have a new prefix?
    if [ -n "$new_ip6_prefix" ] ; then

	# Create an address for iptables rules (also checks prefix length)
	local ASTERISK_ADDR
	if ASTERISK_ADDR=$(v6merge $new_ip6_prefix ff::1) ; then

	    # We have a usable prefix; (re-)create the iptables rules.
	    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p icmpv6 -j ACCEPT
	    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p tcp -m tcp --dport 80 -j ACCEPT
	    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p tcp -m tcp --dport 443 -j ACCEPT
	    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p tcp -m tcp --dport 32698 -j ACCEPT
	    /usr/sbin/ip6tables -A FWD-INET6-IN -d $ASTERISK_ADDR -p tcp -m tcp --dport 32789:32790 -j ACCEPT

	    # Create the route, if it doesn't already exist.
	    if [ -z "$(/usr/sbin/ip -6 route show $new_ip6_prefix)" ] ; then
        	/usr/sbin/ip -6 route add $new_ip6_prefix via fd00:dcaf:bad:ff::1
	    fi
	fi
    else
	/usr/bin/logger "ip6_prefix: removing old prefix: $old_ip6_prefix"
    fi

    # If there is no old prefix, we're done.
    [ -z "$old_ip6_prefix" ] && return

    # If the prefix didn't change, we're done.
    [ "$old_ip6_prefix" = "$new_ip6_prefix" ] && return

    # Clear the old route, if it exists.
    if [ -n "$(/usr/sbin/ip -6 route show $old_ip6_prefix)" ] ; then
	/usr/sbin/ip -6 route del $old_ip6_prefix
    fi
}

ip6_prefix
