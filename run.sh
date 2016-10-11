#!/bin/bash

trap 'exit 1;' SIGABRT SIGSEGV

usage()
{
cat << EOF

Usage: sudo $0 <initial_chunk_size_in_kb> <connections> <comma_sep_intf> <comma_sep_ips> <target_file> <initial_alpha> <processing_skips>
This program downloads an HTTP object from multiple web servers
over multiple TCP connections using multiple interfaces.

OPTIONS:
    <initial_chunk_size_in_kb>  the initial size of first chunk to be delivered with one HTTP response. (in KB)
    <connections>               the number of connections to be established.
    <comma_sep_ints>            interfaces to be used. must be separated by comma without any blank; 0 for automatic collection.
    <comma_sep_ips>             target (server) IP addresses to be used. 0 will let multiDNS do the task.
    <target_file>               the target file including full domain and url. (e.g., http://sphotos-f.ak.fbcdn.net/hsh3/94_n.jpg)

EXAMPLES:
    sudo $0 64 3 eth0,wlan0 192.168.1.1,192.168.2.1,192.168.3.1 http://www.example.com/target.pdf
    ::: This will open 3 connections to web servers with 64KB as the intial HTTP chunk size.
    ::: Initial alpha value is set to 40
    ::: However, only two interfaces and are specified, thus the third connection
    ::: (to 192.168.3.1) will be opened again on the first interface (eth0).

    sudo $0 64 2 0 0 http://www.example.com/target.pdf
    ::: This will open 2 connections. Interfaces and IP addresses will be
    ::: automatically collected.
EOF
}

if [[ -z $1 ]] || [[ -z $2 ]] || [[ -z $3 ]] || [[ -z $4 ]] || [[ -z $5 ]]

then
    clear
    usage
    exit 1
fi

chunk_size=$1
conn_count=$2
interfaces=$3
ipaddrs=$4
target=$5

#max_req_mpsocket=$9
application=wget
app_options=" -e robots=off -E -H -k -K -v -t 1 --no-check-certificate --no-cache --no-proxy --no-dns-cache -p"

LD_PRELOAD=./libmpsocket.so INITIAL_CHUNK_SIZE_IN_KB=${chunk_size} CONNECTIONS=${conn_count} INTERFACES=${interfaces} IPADDRS=${ipaddrs} ${application} ${app_options} ${target}

exit 0
