#!/bin/bash

trap 'exit 1;' SIGABRT SIGSEGV

usage()
{
cat << EOF

Usage: sudo $0 <initial_chunk_size_in_kb> <connections> <comma_sep_intf> <comma_sep_ips> <target_file> <initial_alpha> <processing_skips> <initial_second_path> <random_path> <log_decisions> <log_traffic> <log_metrics>

This program downloads an HTTP object from multiple web servers
over multiple TCP connections using multiple interfaces.

OPTIONS:
    <initial_chunk_size_in_kb>  the initial size of first chunk to be delivered with one HTTP response. (in KB)
    <max_req_con>               max requests per connection - for measurement: set to 0 for unlimited requests
    <max_req_serv>              max requests per server - for measurement: set to 0 for unlimited requests
    <initial_alpha>             initial alpha value for TCP approach scheduling - should be around 20 to 30
    <connections>               the number of connections to be established.
    <comma_sep_ints>            interfaces to be used. must be separated by comma without any blank; 0 for automatic collection.
    <comma_sep_ips>             target (server) IP addresses to be used. 0 will let multiDNS do the task.
    <target_file>               the target file including full domain and url. (e.g., http://sphotos-f.ak.fbcdn.net/hsh3/94_n.jpg)
	<processing_skips>          number of calculus skips to save processing overhead
	<initial_second_path>       use an initial second path (0: false, 1: true)
	<random_path>               use a random interface to create new path (0: false, 1: true)
	<log_decisions>             log scheduler decisions (0: false, 1: true)
	<log_traffic>               log traffic distribution (0: false, 1: true)
	<log_metrics>               log path characterization (0: false, 1: true)

EXAMPLES:
    sudo $0 64 3 eth0,wlan0 192.168.1.1,192.168.2.1,192.168.3.1 http://www.example.com/target.pdf 15 30 40
    ::: This will open 3 connections to web servers with 64KB as the intial HTTP chunk size.
    ::: Maximum requests per connection is limited to 15
    ::: Maximum requests per server is limited to 30
    ::: Initial alpha value is set to 40
    ::: However, only two interfaces and are specified, thus the third connection
    ::: (to 192.168.3.1) will be opened again on the first interface (eth0).

    sudo $0 64 2 0 0 http://www.example.com/target.pdf 15 30 40
    ::: This will open 2 connections. Interfaces and IP addresses will be
    ::: automatically collected.
EOF
}

if [[ -z $1 ]] || [[ -z $2 ]] || [[ -z $3 ]] || [[ -z $4 ]] || [[ -z $5 ]] || [[ -z $6 ]] || [[ -z $7 ]] || [[ -z $8 ]]

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
initial_alpha=$8

if [[ -z ${11} ]]
then
	processing_skips=-1
else
	processing_skips=${11}
fi
if [[ -z ${12} ]]
then
	initial_second_path=0
else
	initial_second_path=${12}
fi
if [[ -z ${13} ]]
then
	random_path=0
else
	random_path=${13}
fi
if [[ -z ${14} ]]
then
	log_decisions=1
else
	log_decisions=${14}
fi
if [[ -z ${15} ]]
then
	log_traffic=0
else
	log_traffic=${15}
fi
if [[ -z ${16} ]]
then
	log_metrics=0
else
	log_metrics=${16}
fi

#max_req_mpsocket=$9
application=wget
app_options=" -e robots=off -E -H -k -K -v -t 1 --no-check-certificate --no-cache --no-proxy --no-dns-cache -p"

LD_PRELOAD=./libmpsocket.so INITIAL_CHUNK_SIZE_IN_KB=${chunk_size} CONNECTIONS=${conn_count} INITIAL_ALPHA=${initial_alpha} PROCESSING_SKIPS=${processing_skips} INTERFACES=${interfaces} IPADDRS=${ipaddrs} INITIAL_SECOND_PATH=${initial_second_path} RANDOM_PATH=${random_path} LOG_DECISIONS=${log_decisions} LOG_TRAFFIC=${log_traffic} LOG_METRICS=${log_metrics} ${application} ${app_options} ${target}

exit 0
