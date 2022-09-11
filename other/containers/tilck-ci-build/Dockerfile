# version: v2
FROM ubuntu:20.04

# We don't want any of the interactive features of apt, so we just disable them.
ENV DEBIAN_FRONTEND=noninteractive
ENV TCROOT=/tc/toolchain2

# Install the required system packages
RUN apt update && apt upgrade -y && \
apt install git-core gcc g++ gcc-multilib g++-multilib clang cmake \
python3-minimal python3-serial apt gawk unzip parted wget curl \
qemu-system-x86 imagemagick -y

# Create the shared directories will need for all the toolchains
RUN mkdir -p /tc/cache && \
mkdir -p /tc/host_x86_64 && \
mkdir -p /tc/noarch && \
mkdir -p /tc/toolchain2 && \
mkdir -p /tc/toolchain2-gcc64 && \
mkdir -p /tc/toolchain2-gcc73 && \
ln -s /tc/cache /tc/toolchain2/cache && \
ln -s /tc/cache /tc/toolchain2-gcc64/cache && \
ln -s /tc/cache /tc/toolchain2-gcc73/cache && \
ln -s /tc/noarch /tc/toolchain2/noarch && \
ln -s /tc/noarch /tc/toolchain2-gcc64/noarch && \
ln -s /tc/noarch /tc/toolchain2-gcc73/noarch && \
ln -s /tc/host_x86_64 /tc/toolchain2/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc64/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc73/host_x86_64

# Clone Tilck and build the toolchain with the default GCC_TC_VER
RUN git clone https://github.com/vvaltchev/tilck.git /tilck-tmp && \
/tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib busybox && \
/tilck-tmp/scripts/build_toolchain -s gmock gmock_arch gtest gtest_arch && \
/tilck-tmp/scripts/build_toolchain -s lcov libmusl lua micropython ncurses && \
/tilck-tmp/scripts/build_toolchain -s tcc vim gcc_glibc_toolchain

# Switch the TCROOT and GCC_TC_VER to 6.4.0 and re-build the toolchain
ENV TCROOT=/tc/toolchain2-gcc64
ENV GCC_TC_VER=6.4.0

RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib busybox && \
/tilck-tmp/scripts/build_toolchain -s gmock gmock_arch gtest gtest_arch && \
/tilck-tmp/scripts/build_toolchain -s lcov libmusl lua micropython ncurses && \
/tilck-tmp/scripts/build_toolchain -s tcc vim gcc_glibc_toolchain

# Switch the TCROOT and GCC_TC_VER to 7.3.0 and re-build the toolchain
ENV TCROOT=/tc/toolchain2-gcc73
ENV GCC_TC_VER=7.3.0
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s gnuefi mtools zlib busybox && \
/tilck-tmp/scripts/build_toolchain -s gmock gmock_arch gtest gtest_arch && \
/tilck-tmp/scripts/build_toolchain -s lcov libmusl lua micropython ncurses && \
/tilck-tmp/scripts/build_toolchain -s tcc vim gcc_glibc_toolchain

RUN rm -rf /tilck-tmp && rm -rf /tc/cache
