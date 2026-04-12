# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# SOPHGO host tools — a vendor-prebuilt toolchain (cross gcc, mkimage,
# fiptool, etc.) used by the licheerv-nano BSP build of u-boot.
#
# Upstream (github.com/sophgo/host-tools) ships the tarball as a tag
# archive, and the binaries inside are x86_64 Linux ELFs, so there is
# nothing to compile. The package is statically self-contained and
# distro-independent, hence `portable: true` — it lives under
# HOST_DIR_PORTABLE next to the cross-compilers.
#
# Because the binaries are x86_64 Linux ELFs, installation is refused
# on any other host OS / host arch combination.
#
class SophgoToolsPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_sophgo_tools',
      url: GITHUB + '/sophgo/host-tools/archive/refs/tags',
      on_host: true,
      is_compiler: false,
      portable: true,
      arch_list: { "x86_64" => ALL_ARCHS["x86_64"] },
      dep_list: [],
      host_os_list: ["linux"],
      host_arch_list: ["x86_64"],
      board_list: ["licheerv-nano"],
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  # Upstream serves the tarball as `<tag>.tar.gz`; store it in the cache
  # under a qualified name so it doesn't collide with other GitHub tag
  # archives that share the same bare version number.
  def remote_tarname(ver) = "#{ver}.tar.gz"
  def tarname(ver) = "sophgo_host_tools-#{ver}.tar.gz"

  # The only thing we actually consume from the upstream tarball is the
  # prebuilt cross gcc tree; check for it as a sanity guard that the
  # extraction landed where we expect.
  def expected_files = [
    ["gcc", true],
  ]

  # Nothing to build: the tarball already contains x86_64 Linux ELF
  # binaries. The base install_impl flow will check expected_files.
  def install_impl_internal(install_dir) = true
end

pkgmgr.register(SophgoToolsPackage.new())
