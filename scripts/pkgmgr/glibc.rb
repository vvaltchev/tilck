# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

GLIBC_SOURCE = SourceRef.new(
  name: 'glibc',
  url:  'https://ftp.gnu.org/gnu/glibc',
  tarname: ->(ver) { "glibc-#{ver}.tar.xz" },
)

# The oldest kernel our binaries will run on. Independent of both the
# glibc version and the kernel headers built against: newer headers let
# glibc KNOW about modern syscalls, while this makes it emit fallbacks
# for anything added after 4.19 and refuse to start below it.
#
# 4.19 is an LTS from 2018. The oldest build host we support is Ubuntu
# 22.04, on 5.15, so this clears it by a wide margin.
GLIBC_MIN_KERNEL = "4.19"

#
# host_glibc: the C library the whole host stack links against, and
# the reason the stack exists at all.
#
# Built by the SYSTEM compiler, which is legal precisely because we
# build for the host's own triple: a compiler for x86_64-pc-linux-gnu
# is already a valid compiler for our target, so no bootstrap compiler
# is needed and the gcc/glibc cycle a cross toolchain suffers never
# forms here.
#
# THE VERSION IS CONSTRAINED BY THAT CHOICE. Building glibc with the
# system compiler means the version must be one the OLDEST supported
# build host can compile. glibc raised its floor from GCC 6.2 to GCC
# 12.1 in 2.42, and Ubuntu 22.04 — the oldest host we support — ships
# GCC 11.4, so 2.41 is the newest we can bootstrap with. 2.42 fails at
# configure with:
#
#   *** These critical programs are missing or too old: compiler
#
# This is a floor on the FIRST glibc only. Once host_gcc exists, a
# later glibc can be built with it; what cannot be done is bootstrap
# the stack from nothing with a glibc newer than the host compiler
# handles. Raising this version therefore means either dropping Ubuntu
# 22.04 as a build host, or building a bootstrap compiler first.
#
# Installed with --prefix pointing INTO the sysroot rather than into
# this package's own directory. The files live here, but every absolute
# path baked into them names the sysroot, which the symlink farm then
# makes true. Baking this package's own path would work right up until
# a second glibc version existed.
#
# See docs/plans/portable-host-stack.md.
#
class HostGlibcPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_glibc',
      source: GLIBC_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_linux_headers', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

  # The one package that cannot carry an RPATH to its own libc: the
  # libc in question is this package. Without this, its utilities
  # (getconf, gencat, ...) and its gconv modules report ~290
  # violations, all of them "libc.so.6 resolved to the system libc"
  # under an LD_LIBRARY_PATH no real invocation sets.
  # See Package#portability_hostile_check?
  def portability_hostile_check? = false

  def expected_files(ver = nil) = [
    ["install/usr/lib/libc.so.6", false],
    ["install/usr/lib/ld-linux-x86-64.so.2", false],
    ["install/usr/lib/libc.a", false],
    ["install/usr/include/stdio.h", false],
  ]

  def build_steps(ver = nil) = [

    # glibc refuses to be configured in its own source tree.
    Mkdir(path: "build"),

    Within(dir: "build", steps: [
      Run(log: "configure.log", argv: [
        "../configure",

        # Paths as they will be once the sysroot is composed, not as
        # they are in staging.
        "--prefix=$SYSROOT/usr",
        "--with-headers=$host_linux_headers/install/usr/include",
        "--enable-kernel=#{GLIBC_MIN_KERNEL}",

        # Everything in one directory: the loader included. The
        # default splits it into /lib, which would leave the sysroot
        # with two library directories for no benefit here.
        "libc_cv_slibdir=$SYSROOT/usr/lib",

        # Recent GCC finds things to warn about in glibc's own sources
        # that are not ours to fix.
        "--disable-werror",

        # Neither is wanted, and both would add host dependencies.
        "--disable-nscd",
        "--without-selinux",
      ]),
      Run(log: "build.log", argv: ["make", "-j$PAR"]),
      Run(log: "install.log",
          argv: ["make", "install", "DESTDIR=$DESTDIR"]),
    ]),

    # DESTDIR reproduces the whole absolute prefix beneath it. Lift the
    # sysroot fragment out: what remains under install/ is a miniature
    # sysroot (usr/lib, usr/include) the farm can compose directly.
    Mkdir(path: "$INSTALL/install"),
    Move(from: "$DESTDIR$SYSROOT/usr", to: "$INSTALL/install/usr"),
    Prune(),
  ]
end

pkgmgr.register(HostGlibcPackage.new())
