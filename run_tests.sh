#!/bin/bash
set -e

mkdir -p build
cd build
cmake .. -DEXAMPLES=ON
cmake --build .
ctest --output-on-failure
