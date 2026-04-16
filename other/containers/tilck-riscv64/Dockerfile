# version: v7
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt upgrade -y

# Copy Tilck's source to our container
COPY . /tilck-tmp

# -------------------------------------------------------------------------

RUN mkdir -p /tc
ARG TCROOT=/tc
ARG ARCH=riscv64

# -------------------------------------------------------------------------
# Install system dependencies (--contrib includes clang, multilib,
# python testing tools, imagemagick, etc.) and all toolchain packages
# for both GCC versions.
# -------------------------------------------------------------------------
ARG GCC_TC_VER=12.4.0
RUN /tilck-tmp/scripts/build_toolchain --contrib && \
/tilck-tmp/scripts/build_toolchain -n -s ALL

# -------------------------------------------------------------------------
ARG GCC_TC_VER=13.3.0
RUN /tilck-tmp/scripts/build_toolchain -n -s ALL


# Remove Tilck's source and the toolchain cache to save space
RUN rm -rf /tilck-tmp && rm -rf /tc/cache
