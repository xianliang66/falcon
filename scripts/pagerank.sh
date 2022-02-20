#!/bin/sh

cd build/Make+Release &&
  make -j16 &&
  make -j4 pagerank.exe &&
  salloc -N2 -n8 mpirun applications/pagerank/pagerank.exe
