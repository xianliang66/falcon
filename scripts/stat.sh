#!/bin/sh

filename="/home/jinzhang/grappa-cache/build/Make+Release/stat.json"
nmsgs=$( cat $filename | grep "delegate_ops" | grep -oE "[0-9]*" )
echo "Total Messages: $nmsgs"
nreads=$( cat $filename | grep "delegate_reads" | grep -oE "[0-9]*" )
echo "Read Number: $nreads"
nwrites=$( cat $filename | grep "delegate_writes" | grep -oE "[0-9]*" )
echo "Write Number: $nwrites"
n_inv=$( cat $filename | grep "delegate_inv" | grep -oE "[0-9]*" )
echo "Total Invalidations: $n_inv"
n_useless_inv=$( cat $filename | grep "delegate_useless_inv" | grep -oE "[0-9]*" )
echo "Useless Invalidations: $n_useless_inv"
write_latency=$( cat $filename | grep "delegate_write_latency_mean" |
  awk -F, '{print $3}' | awk '{print $2}' )
echo "Write Latency: $write_latency"
read_latency=$( cat $filename | grep "delegate_read_latency_mean" |
  awk -F, '{print $3}' | awk '{print $2}' )
echo "Read Latency: $read_latency"
hit=$( cat $filename | grep "delegate_cache_hit" |
  awk -F, '{print $1}' | awk '{print $2}' )
miss=$( cat $filename | grep "delegate_cache_miss" |
  awk -F, '{print $1}' | awk '{print $2}' )
expire=$( cat $filename | grep "delegate_cache_expired" |
  awk -F, '{print $1}' | awk '{print $2}' )
total=$(( 1+hit+miss+expire ))
hit_rate=$( echo "scale=4;$hit/$total*100" | bc )
miss_rate=$( echo "scale=4;$miss/$total*100" | bc )
expire_rate=$( echo "scale=4;$expire/$total*100" | bc )
echo "Cache Hit: $hit_rate%"
echo "Cache miss: $miss_rate%"
echo "Cache Expire: $expire_rate%"
net_bw=$( cat $filename | grep rdma_message_bytes |
  awk -F, '{print $1}' | awk '{print $2}')
net_bw=$( echo "scale=4;$net_bw/1024/1024/1024" | bc )
echo "Network Bandwidth Consumption: $net_bw GB"

