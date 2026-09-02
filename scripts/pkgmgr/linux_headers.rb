# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LINUX_HEADERS_SOURCE = SourceRef.new(
  name: 'linux_headers',
  url:  'https://cdn.kernel.org/pub/linux/kernel/v6.x',
  tarname: ->(ver) { "linux-#{ver}.tar.xz" },
)

#
# host_linux_headers: the kernel's userspace API headers, the bottom of
# the composed sysroot.
#
# glibc is compiled against these, and everything above it inherits
# them. No compiler is involved: `make headers_install` sanitises and
# copies headers, nothing more, which is why this sits below even
# host_glibc in the build order.
#
# Installs a sysroot-shaped fragment (install/usr/include/...), the
# convention every stack package follows so the symlink farm can
# merge them all without special cases.
#
# An LTS kernel rather than the newest: these headers define the syscall
# surface everything in the stack is compiled against, and there is
# nothing to gain from tracking the tip. Note this is unrelated to the
# OLDEST kernel our binaries will run on — that floor is set separately
# by glibc's --enable-kernel.
#
# See docs/plans/portable-host-stack.md.
#
class HostLinuxHeadersPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_linux_headers',
      source: LINUX_HEADERS_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

  def expected_files(ver = nil) = [
    ["install/usr/include/linux/unistd.h", false],
    ["install/usr/include/asm/unistd.h", false],
    ["install/usr/include/asm-generic/errno.h", false],
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def build_steps = [

    # The kernel names x86_64 "x86"; both 32- and 64-bit headers come
    # out of that one tree.
    Step("headers.log", [
      "make", "headers_install",
      "ARCH=x86",
      "INSTALL_HDR_PATH=$INSTALL/install/usr",
    ]),
  ]

  # A kernel tree is ~1.5 GB extracted and we want a few MB of
  # headers out of it.
  def prune_after_build? = true
end

pkgmgr.register(HostLinuxHeadersPackage.new())
