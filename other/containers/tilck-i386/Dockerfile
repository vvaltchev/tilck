# version: v7
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt upgrade -y

# Copy Tilck's source to our container
COPY . /tilck-tmp

RUN mkdir -p /tc
ARG TCROOT=/tc
ARG ARCH=i386

# Layer 1: system dependencies only (cached until install_pkgs changes)
RUN /tilck-tmp/scripts/build_toolchain --contrib --only-system-deps

# Layer 2 + 3: toolchain packages for both GCC versions.
# First pass: default packages (compilers, core deps).
# Second pass: all remaining installable packages.
ARG GCC_TC_VER=12.4.0
RUN /tilck-tmp/scripts/build_toolchain -n
RUN /tilck-tmp/scripts/build_toolchain -n -s ALL

ARG GCC_TC_VER=13.3.0
RUN /tilck-tmp/scripts/build_toolchain -n
RUN /tilck-tmp/scripts/build_toolchain -n -s ALL

# Remove Tilck's source and the toolchain cache to save space
RUN rm -rf /tilck-tmp && rm -rf /tc/cache
