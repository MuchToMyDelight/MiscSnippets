#!/bin/bash
set -e

cd "$(dirname "$0")"

clang++ -g -O3 -lgflags `llvm-config --ldflags --libs` ./main.cc
./a.out --bcFile ./precompiled_func.bc --var $RANDOM

