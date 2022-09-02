# version: v1
# Also tried to use debian/stable-slim as base, but image size went up compared
# to ubuntu 20.04
FROM ubuntu:20.04
# We don't want any of the interactive features of apt, so we just disable them.
ENV DEBIAN_FRONTEND=noninteractive
ENV TCROOT_PARENT=/tc
RUN apt update && apt upgrade -y && \
apt install git-core gcc g++ cmake imagemagick clang gcc-multilib g++-multilib \
python3-minimal apt gawk unzip parted wget qemu-system-x86 -y && \
mkdir /tc && \
git clone https://github.com/vvaltchev/tilck.git /tilck-tmp && \
/tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib \
busybox gmock gmock_arch gtest gtest_arch lcov libmusl lua micropython ncurses \
tcc vim gcc_glibc_toolchain && \
rm -rf tilck-tmp
