#!/bin/sh

export GLOG_minloglevel=0
cd build/Make+Release &&
  make -j16 &&
  make -j16 graphlab-sssp &&
  salloc -N2 -n32 mpirun applications/graphlab/sssp.exe
