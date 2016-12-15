#!/bin/bash

# Exit on the first failure
set -e

echo 'Installing the necessary packages...'

cmd='sudo apt-get install gcc g++ git nasm gcc-multilib g++-multilib make cmake'
echo $cmd

$cmd

mkdir toolchain
cd toolchain
git clone https://github.com/google/googletest.git
cd googletest/googletest
cmake .
make

