# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LIBSLIRP_SOURCE = SourceRef.new(
  name: 'libslirp',
  url:  'https://gitlab.freedesktop.org/slirp/libslirp/-/archive',
  tarname: ->(ver) { "libslirp-v#{ver}.tar.gz" },
  remote_tarname: ->(ver) { "v#{ver}/libslirp-v#{ver}.tar.gz" },
  fetch_via_git: false,
)

#
# host_libslirp: QEMU's user-mode network backend.
#
# `-netdev user` -- the networking every run_qemu script uses, no root
# and no bridge -- is SLIRP. Up to QEMU 6.2 it was a copy inside the
# QEMU tree; from 7.2 it is this library and nothing else, and a QEMU
# built without it has no such backend:
#
#   qemu-system-i386: -netdev user,id=net0,...: network backend
#   'user' is not compiled into this binary
#
# Built for every QEMU, 6.2 included: its configure prefers the
# library it finds to the copy it carries, and one QEMU linking one
# SLIRP is easier to reason about than one of them linking a 2021
# copy of it.
#
# Pure C on glib, meson-built, sysroot-shaped: pixman's shape.
#
class HostLibslirpPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_libslirp',
      source: LIBSLIRP_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def pkg_dirname = "libslirp"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libslirp.so", false],
    ["install/usr/include/slirp/libslirp.h", false],
    ["install/usr/lib/pkgconfig/slirp.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostLibslirpPackage.new())
