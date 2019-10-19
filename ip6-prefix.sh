###
###	dhclient exit hook scripts are sourced by the main dhclient script
###	(/usr/sbin/dhclient-script), so we can't "exit" from the hook script
###	or mess with the global state.  We wrap everything in a function, so
###	that we can keep it all local.
###

handle_delegated_ipv6_prefix() {

    #
    # The "WAN" interface through which dhclient receives the delegated prefix.
    # Events on other interfaces will be ignored by this hook.
    #
    local WAN_INTERFACE=bond0.256

    #
    # The "router" to which all traffic destined for the delegated prefix will
    # be sent.  This should be a stable address (usually a ULA).
    #
    local LAN_ROUTER=fd00:dcaf:bad:ff::1

    #
    # The routing protocol number used to "tag" routes created by this hook.
    #
    local DENAT_RTPROTO=255

    #
    # The ip6tables chain used to allow incoming traffic.
    #
    local IP6TABLES_CHAIN=FWD-INET6-IN

    #
    # An associative array that describes allowed incoming connections.  Keys
    # are the local portion of the host or network address (which will be
    # combined with the delegated prefix); values are lists of port (or port
    # range) definitions.  Incoming ICMPv6 is implicitly allowed for all listed
    # addresses.
    #
    # NOTE: Although this script handles /48, /52, or /56 delegated prefixes,
    #	    "local" addresses must be compatible with a /56 prefix.  Thus, local
    #	    addresses are the lower 72 bits of the "merged" address.
    #
    declare -A ALLOWED_IN
    ALLOWED_IN['ff::1/128']='80/tcp 443/tcp 32698/tcp 32789:32790/tcp'
    ALLOWED_IN['fa::/64']='5060/udp'

    ###
    ###	    END OF CONFIGURATION
    ###

    [ "$interface" = $WAN_INTERFACE ] || return
    [ -z "$old_ip6_prefix" -a -z "$new_ip6_prefix" ] && return

    # Flush the ip6tables rules; we'll (re-)create them if we have a valid new
    # prefix.  (*New* incoming connections will be blocked in the interim.)
    /usr/sbin/ip6tables -F $IP6TABLES_CHAIN

    # Do we have a new prefix?
    if [ -n "$new_ip6_prefix" ] ; then

	local PREFIXLEN=${new_ip6_prefix#*/}
	if [ $PREFIXLEN -gt 56 -o $PREFIXLEN -lt 48 -o $((PREFIXLEN%4)) != 0 ] ; then
	    /usr/bin/logger "ip6-prefix: ignoring invalid prefix: $new_ip6_prefix"

	else

	    # zero-pad a 16-bit "word" in an IPv6 address
	    padword() {
		local WORD=$1
		while [ $(expr length "$WORD") -lt 4 ] ; do
		    WORD=0${WORD}
		done
		echo $WORD
	    }

	    # construct an IPv6 address from a prefix and a "local" address
	    v6merge() {
		local PREFIX=$1
		local HOST=$2
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
		echo "${W1}:${W2}:${W3}:${W4}${HOST}"
	    }

	    local LOCAL_ADDR
	    for LOCAL_ADDR in ${!ALLOWED_IN[@]} ; do
		local ADDR
		if ADDR=$(v6merge $new_ip6_prefix $LOCAL_ADDR) ; then
		    /usr/sbin/ip6tables -A $IP6TABLES_CHAIN -d $ADDR -p icmpv6 -j ACCEPT
		    local PORT
		    for PORT in ${ALLOWED_IN[$LOCAL_ADDR]} ; do
			/usr/sbin/ip6tables -A $IP6TABLES_CHAIN -d $ADDR -p ${PORT#*/} -m ${PORT#*/} --dport ${PORT%/*} -j ACCEPT
		    done
		fi
	    done

	    unset -f v6merge
	    unset -f padword

	    # Create the route, if it doesn't already exist.
	    if [ -z "$(/usr/sbin/ip -6 route show $new_ip6_prefix)" ] ; then
		/usr/sbin/ip -6 route add $new_ip6_prefix proto $DENAT_RTPROTO via $LAN_ROUTER
	    fi

	fi

    else
	/usr/bin/logger "ip6-prefix: removing old prefix: $old_ip6_prefix"
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

handle_delegated_ipv6_prefix
