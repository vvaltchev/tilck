#!/bin/bash

# Exit on the first failure
set -e

MAIN_DIR=$PWD
export PATH=$MAIN_DIR/build_scripts:$PATH

echo 'Installing the necessary packages...'

cmd='sudo apt-get install gcc g++ git nasm gcc-multilib g++-multilib make cmake binutils binutils-multiarch'
echo $cmd

$cmd

mkdir toolchain
cd toolchain

# Toolchain dir
TC=$PWD


# Build libmusl

pushd .

git clone git://git.musl-libc.org/musl
cd musl
git checkout v1.1.9

mkdir ../musl-install
mkdir ../musl-install/lib

./configure --target=i386 --host=i386 --build=x86_64 --disable-shared --prefix=$TC/musl-install --exec-prefix=$TC/musl-install --syslibdir=$TC/musl-install/lib

make
make install

popd


# Build gtest

pushd .
git clone https://github.com/google/googletest.git
cd googletest/googletest
git checkout release-1.8.0
cmake .
make
popd
