#!/bin/bash

# Exit on the first failure
set -e

MAIN_DIR=$PWD
export PATH=$MAIN_DIR/build_scripts:$PATH

echo 'Installing the necessary packages...'

cmd='sudo apt-get install wget gcc g++ git nasm gcc-multilib g++-multilib make cmake binutils binutils-multiarch mtools'
echo $cmd

$cmd

mkdir toolchain
cd toolchain

# Toolchain dir
TC=$PWD


###############################
# Build diet libc
###############################

pushd .

wget https://www.fefe.de/dietlibc/dietlibc-0.33.tar.bz2
tar xfvj dietlibc-0.33.tar.bz2
cd dietlibc-0.33
sed -i 's/#define WANT_SYSENTER/\/\/#define WANT_SYSENTER/g' dietfeatures.h
sed -i 's/-Wno-unused -Wredundant-decls/-Wno-unused -Wredundant-decls -fno-stack-protector/g' Makefile
# The build of dietlibc fails even when succeeds, when we cross-build for i386
# So, disabling the 'exit on first failure' option.
set +e

make i386 DEBUG=1

# Restore the 'exit on first failure'
set -e

popd


##############################
# Build gtest
##############################

pushd .
git clone https://github.com/google/googletest.git
cd googletest/googletest
git checkout release-1.8.0
cmake .
make
popd


##############################
# Build libmusl
##############################

# pushd .

# git clone git://git.musl-libc.org/musl
# cd musl
# git checkout v1.1.16

# mkdir ../musl-install
# mkdir ../musl-install/lib

# ./configure --target=i386 --host=i386 --build=x86_64 --disable-shared --prefix=$TC/musl-install --exec-prefix=$TC/musl-install --syslibdir=$TC/musl-install/lib

# make
# make install

# popd
