#!/bin/sh

cd build/Make+Release &&
  make -j16 &&
  make -j16 sort.exe &&
  salloc -N1 -n8 mpirun applications/sort/grappa/sort.exe
