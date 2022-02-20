#!/bin/sh

cd build/Make+Release &&
  make -j16 &&
  make -j16 Query.exe &&
  salloc -N1 -n8 mpirun applications/join/Query.exe
