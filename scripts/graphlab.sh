#!/bin/sh

export GLOG_minloglevel=1
cd build/Make+Release &&
  make -j16 &&
  make -j4 graphlab-pagerank &&
  salloc -N1 -n8 mpirun applications/graphlab/pagerank.exe --scale 20
