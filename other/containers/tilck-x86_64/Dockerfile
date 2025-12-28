# version: v6
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# Install the required system packages
RUN apt update && apt upgrade -y && \
apt install -y git-core gcc g++ gcc-multilib g++-multilib clang cmake \
python3-minimal python3-serial python3-pip python3-coverage gawk unzip \
wget curl qemu-system-x86 imagemagick

# Copy Tilck's source to our container
COPY . /tilck-tmp

# Setup the list of packages to download & build for all the compilers
ARG TILCK_PACKAGES="gnuefi mtools zlib busybox gmock gtest lcov ncurses acpica"

# -------------------------------------------------------------------------

RUN mkdir -p /tc
ARG TCROOT=/tc
ARG ARCH=x86_64

# -------------------------------------------------------------------------
ARG GCC_TC_VER=12.4.0
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s $TILCK_PACKAGES

# -------------------------------------------------------------------------
ARG GCC_TC_VER=13.3.0
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s $TILCK_PACKAGES


# Remove Tilck's source and the toolchain cache to save space
RUN rm -rf /tilck-tmp && rm -rf /tc/cache
