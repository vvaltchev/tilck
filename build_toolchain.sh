#!/bin/bash

mkdir toolchain
cd toolchain
git clone https://github.com/google/googletest.git
cd googletest/googletest
cmake .
make

