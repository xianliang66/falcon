#!/bin/sh

cd build/Make+Release &&
  make -j8 &&
  make demo-nqueens &&
  salloc -N1 -n8 mpirun applications/demos/nqueens.exe
