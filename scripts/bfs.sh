#!/bin/sh

export GLOG_minloglevel=0
cd build/Make+Release &&
  make -j16 &&
  make -j16 bfs.exe &&
  salloc -N2 -n16 mpirun applications/nativegraph/bfs/bfs.exe
