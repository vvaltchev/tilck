# version: v9
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# Copy Tilck's source to our container
COPY . /tilck-tmp

RUN mkdir -p /tc
ARG TCROOT=/tc
ARG ARCH=riscv64

# An image build is unattended: the package manager installs what a
# package needs from the system without asking, as it does on a CI
# runner.
ENV RUNNING_IN_CI=1

# Layer 1: base-image refresh + system dependencies. The `apt update` has to
# stay in the same layer as the install it feeds: on its own it stays cached
# for weeks while Ubuntu drops superseded .debs from the pool, so the install
# then asks for versions the stale index still advertises and gets a 404.
RUN apt update && apt upgrade -y && \
    /tilck-tmp/scripts/build_toolchain --contrib --only-system-deps

# Layer 2 + 3: toolchain packages for both GCC versions.
# First pass: default packages (compilers, core deps).
# Second pass: every other Tilck package and the host tools they need.
# ALL stops at the edge of the host world (our own GCC stacks and the
# QEMU built with them): CI runs the distro's QEMU, and those stacks
# are hours of building the kernel tests never use.
ARG GCC_TC_VER=12.4.0
RUN /tilck-tmp/scripts/build_toolchain -n
RUN /tilck-tmp/scripts/build_toolchain -n -s ALL

ARG GCC_TC_VER=13.3.0
RUN /tilck-tmp/scripts/build_toolchain -n
RUN /tilck-tmp/scripts/build_toolchain -n -s ALL

# Remove Tilck's source and the toolchain cache to save space
RUN rm -rf /tilck-tmp /tc/cache
