# version: v5
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# Install the required system packages
RUN apt update && apt upgrade -y && \
apt install -y git-core gcc g++ gcc-multilib g++-multilib clang cmake \
python3-minimal python3-serial python3-pip python3-coverage gawk unzip \
parted wget curl qemu-system-x86 imagemagick

# Copy Tilck's source to our container
COPY . /tilck-tmp

# Setup the list of packages to download & build for all the compilers
ARG TILCK_PACKAGES="gnuefi mtools zlib busybox gmock gtest lcov ncurses acpica"

# -------------------------------------------------------------------------

# Create the shared directories we'll need for all the toolchains
RUN mkdir -p /tc/cache && \
mkdir -p /tc/host_x86_64 && \
mkdir -p /tc/noarch && \
mkdir -p /tc/toolchain3-gcc113 && \
mkdir -p /tc/toolchain3-gcc123 && \
ln -s /tc/cache /tc/toolchain3-gcc113/cache && \
ln -s /tc/cache /tc/toolchain3-gcc123/cache && \
ln -s /tc/noarch /tc/toolchain3-gcc113/noarch && \
ln -s /tc/noarch /tc/toolchain3-gcc123/noarch && \
ln -s /tc/host_x86_64 /tc/toolchain3-gcc113/host_x86_64 && \
ln -s /tc/host_x86_64 /tc/toolchain3-gcc123/host_x86_64



# -------------------------------------------------------------------------
ARG TCROOT=/tc/toolchain3-gcc113
ARG GCC_TC_VER=11.3.0
ARG ARCH=x86_64
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s $TILCK_PACKAGES

# -------------------------------------------------------------------------
ARG TCROOT=/tc/toolchain3-gcc123
ARG GCC_TC_VER=12.3.0
ARG ARCH=x86_64
RUN /tilck-tmp/scripts/build_toolchain && \
/tilck-tmp/scripts/build_toolchain -s $TILCK_PACKAGES


# Remove Tilck's source and the toolchain cache to save space
RUN rm -rf /tilck-tmp && rm -rf /tc/cache
