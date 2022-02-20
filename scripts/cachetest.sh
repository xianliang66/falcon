#!/bin/sh

export GLOG_minloglevel=2
cd build/Make+Release &&
  make -j16 &&
  make -j16 demo-hello_world &&
  salloc -N2 -n4 mpirun applications/demos/hello_world.exe
