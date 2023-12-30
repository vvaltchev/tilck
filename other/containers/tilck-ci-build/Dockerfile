# version: v3
FROM ubuntu:20.04

# We don't want any of the interactive features of apt, so we just disable them.
ENV DEBIAN_FRONTEND=noninteractive

# Install the required system packages
RUN apt update && apt upgrade -y && \
apt install git-core gcc g++ gcc-multilib g++-multilib clang cmake \
python3-minimal python3-serial python3-pip apt gawk unzip parted wget \
curl qemu-system-x86 imagemagick -y

# Create the shared directories will need for all the toolchains
RUN mkdir -p /tc/cache && \
mkdir -p /tc/host_x86_64 && \
mkdir -p /tc/noarch && \
mkdir -p /tc/toolchain2-gcc64 && \
mkdir -p /tc/toolchain2-gcc84 && \
mkdir -p /tc/toolchain2-gcc73 && \
mkdir -p /tc/toolchain2-gcc93 && \
mkdir -p /tc/toolchain2-gcc84-x86-64 && \
ln -s /tc/cache /tc/toolchain2-gcc64/cache && \
ln -s /tc/cache /tc/toolchain2-gcc73/cache && \
ln -s /tc/cache /tc/toolchain2-gcc84/cache && \
ln -s /tc/cache /tc/toolchain2-gcc84-x86-64/cache && \
ln -s /tc/cache /tc/toolchain2-gcc93/cache && \
ln -s /tc/noarch /tc/toolchain2-gcc64/noarch && \
ln -s /tc/noarch /tc/toolchain2-gcc73/noarch && \
ln -s /tc/noarch /tc/toolchain2-gcc84/noarch && \
ln -s /tc/noarch /tc/toolchain2-gcc84-x86-64/noarch && \
ln -s /tc/noarch /tc/toolchain2-gcc93/noarch && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc64/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc73/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc84/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc84-x86-64/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc93/host_x86_64

# -------------------------------------------------------------------------
ENV TCROOT=/tc/toolchain2-gcc84
ENV GCC_TC_VER=8.4.0
ENV ARCH=i386
RUN git clone https://github.com/vvaltchev/tilck.git /tilck-tmp && \
/tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib busybox && \
/tilck-tmp/scripts/build_toolchain -s gmock gmock_arch gtest gtest_arch && \
/tilck-tmp/scripts/build_toolchain -s lcov libmusl lua micropython ncurses && \
/tilck-tmp/scripts/build_toolchain -s tcc vim gcc_glibc_toolchain

# -------------------------------------------------------------------------
ENV TCROOT=/tc/toolchain2-gcc84-x86-64
ENV GCC_TC_VER=8.4.0
ENV ARCH=x86_64
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib busybox && \
/tilck-tmp/scripts/build_toolchain -s gmock gmock_arch gtest gtest_arch && \
/tilck-tmp/scripts/build_toolchain -s lcov libmusl lua micropython ncurses && \
/tilck-tmp/scripts/build_toolchain -s tcc vim gcc_glibc_toolchain

# -------------------------------------------------------------------------
ENV TCROOT=/tc/toolchain2-gcc64
ENV GCC_TC_VER=6.4.0
ENV ARCH=i386
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib busybox && \
/tilck-tmp/scripts/build_toolchain -s gmock gmock_arch gtest gtest_arch && \
/tilck-tmp/scripts/build_toolchain -s lcov libmusl lua micropython ncurses && \
/tilck-tmp/scripts/build_toolchain -s tcc vim gcc_glibc_toolchain

# -------------------------------------------------------------------------
ENV TCROOT=/tc/toolchain2-gcc73
ENV GCC_TC_VER=7.3.0
ENV ARCH=i386
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib busybox && \
/tilck-tmp/scripts/build_toolchain -s gmock gmock_arch gtest gtest_arch && \
/tilck-tmp/scripts/build_toolchain -s lcov libmusl lua micropython ncurses && \
/tilck-tmp/scripts/build_toolchain -s tcc vim gcc_glibc_toolchain

# -------------------------------------------------------------------------
ENV TCROOT=/tc/toolchain2-gcc93
ENV GCC_TC_VER=9.3.0
ENV ARCH=i386
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib busybox && \
/tilck-tmp/scripts/build_toolchain -s gmock gmock_arch gtest gtest_arch && \
/tilck-tmp/scripts/build_toolchain -s lcov libmusl lua micropython ncurses && \
/tilck-tmp/scripts/build_toolchain -s tcc vim gcc_glibc_toolchain

# -------------------------------------------------------------------------
ENV GCC_TC_VER=
ENV ARCH=
RUN rm -rf /tilck-tmp && rm -rf /tc/cache
