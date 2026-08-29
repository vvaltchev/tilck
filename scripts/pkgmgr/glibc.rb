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
# host_glibc: the C library the whole hermetic stack links against, and
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
# See docs/plans/hermetic-host-toolchain.md.
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
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_linux_headers', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

  def expected_files(ver = nil) = [
    ["install/usr/lib/libc.so.6", false],
    ["install/usr/lib/ld-linux-x86-64.so.2", false],
    ["install/usr/lib/libc.a", false],
    ["install/usr/include/stdio.h", false],
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  # Absolute path of the installed kernel headers, which glibc compiles
  # against. Taken from the package rather than the sysroot: the farm
  # has not composed the sysroot yet at this point in the build order.
  def linux_headers_include
    pkg = pkgmgr.get("host_linux_headers")
    return pkg.install_prefix(pkg.default_ver) / "install/usr/include"
  end

  def install_impl_internal(install_dir)

    # Paths as they will be once the sysroot is composed, not as they
    # are in staging.
    sysroot_usr = "#{hermetic_sysroot}/usr"
    destdir = "#{install_dir}/destdir"

    # glibc refuses to be configured in its own source tree.
    FileUtils.mkdir_p("build")

    conf = [
      "../configure",
      "--prefix=#{sysroot_usr}",
      "--with-headers=#{linux_headers_include}",
      "--enable-kernel=#{GLIBC_MIN_KERNEL}",

      # Everything in one directory: the loader included. The default
      # splits it into /lib, which would leave the sysroot with two
      # library directories for no benefit here.
      "libc_cv_slibdir=#{sysroot_usr}/lib",

      # Recent GCC finds things to warn about in glibc's own sources
      # that are not ours to fix.
      "--disable-werror",

      # Neither is wanted, and both would add host dependencies.
      "--disable-nscd",
      "--without-selinux",
    ]

    ok = false
    chdir("build") do
      ok = run_command("configure.log", conf)
      next if !ok

      ok = run_command("build.log", ["make", "-j#{BUILD_PAR}"])
      next if !ok

      ok = run_command("install.log",
                       ["make", "install", "DESTDIR=#{destdir}"])
    end

    return false if !ok

    # DESTDIR reproduces the whole absolute prefix beneath it. Lift the
    # sysroot fragment out: what remains under install/ is a miniature
    # sysroot (usr/lib, usr/include) the farm can compose directly.
    FileUtils.mkdir_p("#{install_dir}/install")
    FileUtils.mv("#{destdir}#{sysroot_usr}", "#{install_dir}/install/usr")

    Dir.children(".").each { |e|
      next if e == "install"
      rm_rf(e)
    }
    return true
  end
end

pkgmgr.register(HostGlibcPackage.new())
