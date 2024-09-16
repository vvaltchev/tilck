# version: v1
FROM ubuntu:22.04

# We don't want any of the interactive features of apt, so we just disable them.
ENV DEBIAN_FRONTEND=noninteractive

# Install the required system packages
RUN apt update && apt upgrade -y && \
apt install -y git-core gcc g++ gcc-multilib g++-multilib clang cmake \
python3-minimal python3-serial python3-pip gawk unzip parted wget \
curl qemu-system-x86 imagemagick

# Checkout the Tilck project to /tilck-tmp
RUN git clone https://github.com/vvaltchev/tilck.git /tilck-tmp

# Setup the list of packages to download & build for all the compilers
ARG TILCK_PACKAGES="gnuefi mtools zlib busybox \
gmock gmock_arch gtest gtest_arch lcov libmusl \
lua micropython ncurses tcc vim gcc_glibc_toolchain"

# -------------------------------------------------------------------------

# Create the shared directories we'll need for all the toolchains
RUN mkdir -p /tc/cache && \
mkdir -p /tc/host_x86_64 && \
mkdir -p /tc/noarch && \
mkdir -p /tc/toolchain2-gcc93 && \
mkdir -p /tc/toolchain2-gcc103 && \
mkdir -p /tc/toolchain2-gcc113 && \
ln -s /tc/cache /tc/toolchain2-gcc93/cache && \
ln -s /tc/cache /tc/toolchain2-gcc103/cache && \
ln -s /tc/cache /tc/toolchain2-gcc113/cache && \
ln -s /tc/noarch /tc/toolchain2-gcc93/noarch && \
ln -s /tc/noarch /tc/toolchain2-gcc103/noarch && \
ln -s /tc/noarch /tc/toolchain2-gcc113/noarch && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc93/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc103/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain2-gcc113/host_x86_64


# -------------------------------------------------------------------------
ARG TCROOT=/tc/toolchain2-gcc93
ARG GCC_TC_VER=9.3.0
ARG ARCH=x86_64
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s $TILCK_PACKAGES

# -------------------------------------------------------------------------
ARG TCROOT=/tc/toolchain2-gcc103
ARG GCC_TC_VER=10.3.0
ARG ARCH=x86_64
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s $TILCK_PACKAGES

# -------------------------------------------------------------------------
ARG TCROOT=/tc/toolchain2-gcc113
ARG GCC_TC_VER=11.3.0
ARG ARCH=x86_64
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s $TILCK_PACKAGES


# Remove Tilck's source and the toolchain cache to save space
RUN rm -rf /tilck-tmp && rm -rf /tc/cache
