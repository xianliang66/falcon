#!/bin/sh

export GLOG_minloglevel=0

vanilla=0
wi=1
tardis=2
cd build/Make+Release &&
  make -j16 &&
  make -j16 demo-micro &&
  salloc -N1 -n16 mpirun applications/demos/micro.exe \
   --cache_proto=$vanilla
  sh ../../stat.sh
  salloc -N1 -n16 mpirun applications/demos/micro.exe \
   --max_cache_number=50000000 --cache_proto=$wi
  sh ../../stat.sh
  salloc -N1 -n16 mpirun applications/demos/micro.exe \
   --max_cache_number=50000000 --cache_proto=$tardis
  sh ../../stat.sh
  salloc -N1 -n16 mpirun applications/demos/micro.exe \
   --max_cache_number=1000 --cache_proto=$wi
  sh ../../stat.sh
  salloc -N1 -n16 mpirun applications/demos/micro.exe \
   --max_cache_number=1000 --cache_proto=$tardis
