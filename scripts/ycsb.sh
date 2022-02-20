#!/bin/sh

export GLOG_minloglevel=1
vanilla=0
tardis=1
wi=2
cd build/Make+Release &&
  make -j16 &&
  make -j16 demo-ycsb

object_number=250000

for read_ratio in 0.9 0.99 0.999
do
for alpha in 0.99
do
for cache_percentage in 10
do
  cache_number=$(awk "BEGIN {print $object_number*$cache_percentage/100}")

  for proto in $wi
  do
  echo "Read: $read_ratio"
  echo "Alpha; $alpha"
  echo "Cache: $cache_percentage%"
  echo "Proto: $proto"
  salloc -N1 -n16 mpirun applications/kv/ycsb.exe \
    -constant=false \
    -alpha=$alpha \
    -read_propotion=$read_ratio \
    -cache_proto=$proto \
    -max_cache_number=$cache_number \
    -loop_threshold=120 &&
    sh ../../stat.sh
  done

done
done
done

