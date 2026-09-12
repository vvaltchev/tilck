# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LIBFFI_SOURCE = SourceRef.new(
  name: 'libffi',
  url:  GITHUB + '/libffi/libffi',
)

#
# host_libffi: glib's foreign-function interface, and the first
# autotools package of the host stack.
#
class HostLibffiPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_libffi',
      source: LIBFFI_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gcc', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

  def expected_files(ver = nil) = [
    ["install/usr/lib/libffi.so", false],
    ["install/usr/include/ffi.h", false],
    ["install/usr/lib/pkgconfig/libffi.pc", false],
  ]

  def build_env(ver)

    prefix = install_prefix(ver) / "install" / "usr"

    return BuildEnv.new(
      include_dirs:    [prefix / "include"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end

  def build_flags(ver = nil) = [
      "--disable-static",       # the sysroot ships shared libraries
      "--libdir=$SYSROOT/usr/lib",
      "--disable-multi-os-directory",  # keeps it out of lib64
  ]

  # autogen runs OUTSIDE the stack's toolchain, as it did: it builds a
  # configure script with the host's autotools.
  def build_steps(ver = default_ver) = [
    # The git tarball has no configure script; autogen builds one.
    Run(log: "autogen.log", argv: ["./autogen.sh"]),
    *autotools_stack_steps(build_flags(ver)),
  ]
end

pkgmgr.register(HostLibffiPackage.new())
