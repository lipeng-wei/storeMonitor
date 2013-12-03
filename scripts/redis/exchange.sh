#!/bin/bash
#by chendazhuang@baidu.com

script_dir="$(dirname $0)"
log_file="$script_dir/../../log/exchange.log"
log_wf_file="$script_dir/../../log/exchange.log.wf"

#for manual exec
#log_file="/dev/fd/1"
#log_wf_file="/dev/fd/2"

pid="$1"
oldip="$2"
newip="$3"

function global_env_init() {
base_dir="/home/arch"
php="/home/arch/applib/php5/bin/php"
#base_dir="/home/arch/dzch"
#php="/home/arch/odp/php/bin/php"
scp_timeout=5 #second

#bj
zk_host[0]="10.23.253.43:8181,10.23.247.141:8181,10.65.27.21:8181,10.81.7.200:8181,10.65.39.219:8181"
#hz
zk_host[1]="10.212.143.34:8181,10.212.130.33:8181,10.212.156.24:8181,10.212.127.32:8181"
#rd
#zk_host[2]="10.46.135.28:2181,10.46.135.29:2181"

zk_redisproxy="/baidu/ns/ksarch/redisproxy/$pid"
proxy_ips=""

oldhostname=$(host $oldip | gawk '{print $5}')
newhostname=$(host $newip | gawk '{print $5}')

proxy_dir="$base_dir/redisproxy-$(echo $pid | sed 's#_#-#g')"
new_conf="$pid.proxy.conf.new"
old_conf="$pid.proxy.conf"

mkdir -p zkvar
z="$php $script_dir/z"
log_notice "exchange begin..."
}

function log_warning () {
echo "$(date +'%m-%d %H:%M:%S'): WARNING: [$pid] $1" >> $log_wf_file
}

function log_notice () {
echo "$(date +'%m-%d %H:%M:%S'): NOTICE: [$pid] $1" >> $log_file
}

function err_exit () {
log_warning "exchange exit with error"
log_notice "exchange exit with error"
exit -1
}

function succ_exit () {
log_notice "exchange success and exit"
exit 0
}

function myscp () {
#for test
#echo $1$2 | grep "10.46.135.26" && cmd="scp $1 $2" || cmd="scp -l 1 $1 $2"
local cmd="scp $1 $2"
local ret=
set +m
trap "" ALRM TERM
(trap - ALRM TERM; eval $cmd)&
local cmd_pid=$!
(trap - ALRM TERM; sleep $scp_timeout; kill -TERM $cmd_pid)&
local alarm_pid=$!
wait $cmd_pid
ps | grep -q $alarm_pid && ret=0 || ret=1
kill -ALRM $alarm_pid >/dev/null 2>&1
kill -TERM $alarm_pid >/dev/null 2>&1
trap - ALRM TERM
return $ret
}

function get_proxy_ip_in_zk () {
local zk_host="$1" 
$z sh $zk_host
if [ $? -ne 0 ];then
    log_warning "fail to connect zk[$zk_host]"
    err_exit
fi

local proxy_nodes=$($z ls $zk_redisproxy)
if [ "$proxy_nodes" == "node $zk_redisproxy doesn't exist or could not connect to zookeeper" ];then
    log_warning "no child found in zk[$zk_host] path[$zk_redisproxy]"
    return
fi

for proxy_node in $proxy_nodes;do
    local proxy_node_value=$($z get $zk_redisproxy/$proxy_node)
    if [ -z "$proxy_node_value" ];then
	log_warning "fail to get $proxy_node"
	err_exit
    fi
    local proxy_ip=$(echo $proxy_node_value | gawk '{match($0, /([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)/, arr); print arr[1]}')
    if [ -z "$proxy_ip" ];then
	log_warning "no ip fond in node[$proxy_node] value[$proxy_node_value]"
	err_exit
    fi
    proxy_ips="$proxy_ips $proxy_ip"
done
}

function get_proxy_ips () {
proxy_ips=""
local zk_host_num=${#zk_host[*]}
for (( i = 0; i < $zk_host_num; i ++ ));do
    get_proxy_ip_in_zk "${zk_host[$i]}"
done
if [ -z "$proxy_ips" ];then
    log_warning "no proxys found for $pid"
    err_exit
else
    log_notice "proxy_ips: [$proxy_ips]"
fi
}

function download_proxy_conf () {
log_notice "download proxy.conf..."
[[ -f $old_conf ]] && rm $old_conf
for proxy_ip in $proxy_ips;do
    myscp arch@$proxy_ip:$proxy_dir/conf/proxy.conf $old_conf
    local ret=$?
    if [ $ret -ne 0 ];then
	log_warning "scp proxy.conf timeout from $proxy_ip"
	continue
    fi
    if [ -f "$old_conf" ];then
	break
    fi
done
if [ $ret -ne 0 -o ! -f "$old_conf" ];then
    log_warning "fail to scp proxy.conf to local"
    err_exit
fi
}

function find_old_master_shards () {
local service_module=""
local in_module=0
local is_old_master=0
local is_new_master=0

old_master_shards=()
while read line;do
    if [ $in_module -eq 0 ];then
	echo $line | grep -e "^[[:space:]]*\[.@Service\]" -q
	if [ $? -eq 0 ];then
	    service_module="$line"
	    in_module=1	
	    is_old_master=0
	    is_new_master=0
	    continue
	else
	    continue
	fi
    else
	echo $line | grep -e "^[[:space:]]*\[\.*@.*\]" -q
	if [ $? -eq 0 ];then
	    # former module done
	    if [ $is_old_master -eq 1 ];then
		temp_old_master_shard=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Partition" | gawk -F: '{print $2}'|sed 's/[[:space:]]//g');
		old_master_shards[$temp_old_master_shard]=1
	    elif [ $is_new_master -eq 1 ];then
		# new master
		:
	    else
		# normal
		:
	    fi

	    in_module=0
	    service_module=""
	    is_new_master=0
	    is_old_master=0

	    # is new service_module?
	    echo $line | grep -e "^[[:space:]]*\[.@Service\]" -q
	    if [ $? -eq 0 ];then
		service_module="$line"
		in_module=1	
	    else
		# nothing
		:
	    fi
	else
	    # contine this service_module
	    service_module="$service_module 
	    $line"
	    if [ $is_old_master -eq 1 -o $is_new_master -eq 1 ];then
		continue
	    else
		echo $line | grep "$oldip" -q
		if [ $? -eq 0 ];then
		    is_old_master=1
		else
		    echo $line | grep "$newip" -q
		    if [ $? -eq 0 ];then
			is_new_master=1
		    fi
		fi
	    fi
	fi
    fi
done < $old_conf
}

function modify_proxy_conf () {
local service_module=""
local in_module=0
local is_old_master=0
local is_new_master=0

new_master_port=
slaves_ip_port_shard=""

:> $new_conf
while read line;do
    if [ $in_module -eq 0 ];then
	echo $line | grep -e "^[[:space:]]*\[.@Service\]" -q
	if [ $? -eq 0 ];then
	    service_module="$line"
	    in_module=1	
	    is_old_master=0
	    is_new_master=0
	    continue
	else
	    echo "$line" >> $new_conf
	    continue
	fi
    else
	echo $line | grep -e "^[[:space:]]*\[\.*@.*\]" -q
	if [ $? -eq 0 ];then
	    # former module done, write
	    if [ $is_old_master -eq 1 ];then
		log_notice "oldmaster:"
		service_module=$(echo "$service_module" | \
		sed -r 's/(^[[:space:]]*Service_CanRead[[:space:]]*:[[:space:]]*)(.*)$/\10/g' | \
		sed -r 's/(^[[:space:]]*Service_CanWrite[[:space:]]*:[[:space:]]*)(.*)$/\10/g')
		log_notice "$service_module"
	    elif [ $is_new_master -eq 1 ];then
		temp_new_master_port=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Port" | gawk -F: '{print $2}'|sed 's/[[:space:]]//g');
		temp_new_master_shard=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Partition" | gawk -F: '{print $2}'|sed 's/[[:space:]]//g');
		if [ -z "${old_master_shards[$temp_new_master_shard]}" ];then
		    log_notice "normalmodule:"
		    log_notice "$service_module"
		    slave_port=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Port" | gawk -F: '{print $2}' | sed 's/[[:space:]]//g');
		    slave_ip=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Addr" | gawk -F: '{print $2}' | sed 's/[[:space:]]//g');
		    slave_shard=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Partition" | gawk -F: '{print $2}' | sed 's/[[:space:]]//g');
		    slaves_ip_port_shard="$slaves_ip_port_shard $slave_ip:$slave_port:$slave_shard"
		else
		    log_notice "newmaster:"
		    service_module=$(echo "$service_module" | \
		    sed -r 's/(^[[:space:]]*Service_CanRead[[:space:]]*:[[:space:]]*)(.*)$/\11/g' | \
		    sed -r 's/(^[[:space:]]*Service_CanWrite[[:space:]]*:[[:space:]]*)(.*)$/\11/g')
		    log_notice "$service_module"
		    temp_new_master_port=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Port" | gawk -F: '{print $2}'|sed 's/[[:space:]]//g');
		    temp_new_master_shard=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Partition" | gawk -F: '{print $2}'|sed 's/[[:space:]]//g');
		    new_master_port="$new_master_port $temp_new_master_port"
		    shard_new_master_port[$temp_new_master_shard]=$temp_new_master_port
		fi
	    else
		log_notice "normalmodule:"
		log_notice "$service_module"
		slave_port=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Port" | gawk -F: '{print $2}' | sed 's/[[:space:]]//g');
		slave_ip=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Addr" | gawk -F: '{print $2}' | sed 's/[[:space:]]//g');
		slave_shard=$(echo "$service_module" | grep -e "^[[:space:]]*Service_Partition" | gawk -F: '{print $2}' | sed 's/[[:space:]]//g');
		slaves_ip_port_shard="$slaves_ip_port_shard $slave_ip:$slave_port:$slave_shard"
	    fi

	    # write to new conf
	    echo "$service_module" >> $new_conf
	    in_module=0
	    service_module=""
	    is_new_master=0
	    is_old_master=0

	    # is new service_module?
	    echo $line | grep -e "^[[:space:]]*\[.@Service\]" -q
	    if [ $? -eq 0 ];then
		service_module="$line"
		in_module=1	
	    else
		echo "$line" >> $new_conf
	    fi
	else
	    # contine this service_module
	    service_module="$service_module 
	    $line"
	    if [ $is_old_master -eq 1 -o $is_new_master -eq 1 ];then
		continue
	    else
		echo $line | grep "$oldip" -q
		if [ $? -eq 0 ];then
		    is_old_master=1
		else
		    echo $line | grep "$newip" -q
		    if [ $? -eq 0 ];then
			is_new_master=1
		    fi
		fi
	    fi
	fi
    fi
done < $old_conf
}

function upload_proxy_conf () {
uploaded_ips=""
for proxy_ip in $proxy_ips;do
    myscp $new_conf arch@$proxy_ip:$proxy_dir/conf/proxy.conf 
    if [ $? -eq 0 ];then
	uploaded_ips="$uploaded_ips $proxy_ip"
    else
	#roll back
	log_warning "upload new proxy.conf to $proxy_ip timeout, please check this ip or REMOVE it from zk"
	rollback_proxy_conf
	err_exit
	break
    fi
done
log_notice "uploade new_proxy.conf done"
}

function rollback_proxy_conf () {
for uploaded_ip in $uploaded_ips;do
    scp $old_conf arch@$uploaded_ip:$proxy_dir/conf/proxy.conf
done
log_warning "roll back proxy.conf done, exchange fail"
}

function restart_proxy () {
for proxy_ip in $proxy_ips;do
    ssh arch@$proxy_ip \
    "$proxy_dir/bin/redisproxy_control restart"
done
log_notice "restart proxy done"
}

function start_new_master () {
for newport in $new_master_port;do
    $script_dir/redis-cli -h $newip -p $newport SLAVEOF NO ONE
    $script_dir/redis-cli -h $newip -p $newport INFO | grep "role:master" -q
    if [ $? -ne 0 ];then
	log_warning "SLAVEOF NO ONE failed for $newip:$newport"
	rollback_proxy_conf 
	err_exit
    fi
done
log_notice "start new master done"
}

function change_master_of_slaves () {
for slave in $slaves_ip_port_shard;do
    slave_ip=$(echo $slave | gawk -F: '{print $1}')
    slave_port=$(echo $slave | gawk -F: '{print $2}')
    slave_shard=$(echo $slave | gawk -F: '{print $3}')
    if [ -z "${shard_new_master_port[$slave_shard]}" ];then
	continue
    fi
    while true;do
	log_notice "$script_dir/redis-cli -h $slave_ip -p $slave_port SLAVEOF $newip ${shard_new_master_port[$slave_shard]}"
	$script_dir/redis-cli -h $slave_ip -p $slave_port SLAVEOF $newip ${shard_new_master_port[$slave_shard]}
	local info=$($script_dir/redis-cli -h $slave_ip -p $slave_port info)
	echo $info | grep -q "master_host:$newip" && echo $info | grep -q "master_port:${shard_new_master_port[$slave_shard]}" && break
	sleep 1
    done
    while true;do
	info=$($script_dir/redis-cli -h $slave_ip -p $slave_port info)
	echo $info | grep -q "master_sync_in_progress:0" && break || sleep 5
    done
    log_notice "slave [$slave_ip:$slave_port] change master to [$newip:${shard_new_master_port[$slave_shard]}] done"
done 
}

function notice_manual_jobs () {
# modify redis.conf
log_notice '****注意:****\n" \
"* 请修改新主机的redis.conf的配置,注视掉slaveof xxx\n" \
"* 请修该pid的其他redis的redis.conf, 修改slaveof配置为slaveof对应的master\n" \
"* 重启原主机后，修改proxy.conf中该主机对应的CanRead为1\n"'
}

function main () {
global_env_init
get_proxy_ips
download_proxy_conf
find_old_master_shards 
modify_proxy_conf
upload_proxy_conf
start_new_master
restart_proxy
change_master_of_slaves
notice_manual_jobs
succ_exit
}

if [ $# -ne 3 ];then
    log_notice "usage $0 \$pid \$oldmasterip \$newmasterip"
    exit -1
fi

main
