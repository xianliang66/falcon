#!/bin/sh

cd build/Make+Release &&
  make -j16 &&
  make -j4 label.exe &&
  salloc -N2 -n8 mpirun applications/nativegraph/label/label.exe
