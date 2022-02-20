#!/bin/sh

export GLOG_minloglevel=0
cd build/Make+Release &&
  make -j16 &&
  make -j16 isopath.exe &&
  salloc -N2 -n8 mpirun applications/isopath/grappa/isopath.exe --scale 7
