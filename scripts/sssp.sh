#!/bin/sh

export GLOG_minloglevel=0
cd build/Make+Release &&
  make -j16 &&
  make -j16 sssp.exe &&
  salloc -N1 -n16 mpirun applications/nativegraph/sssp/sssp.exe \
--num_starting_workers 24
