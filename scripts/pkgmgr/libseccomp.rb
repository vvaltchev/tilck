# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LIBSECCOMP_SOURCE = SourceRef.new(
  name: 'libseccomp',
  url:  GITHUB + '/seccomp/libseccomp/releases/download',
  tarname: ->(ver) { "libseccomp-#{ver}.tar.gz" },
  remote_tarname: ->(ver) { "v#{ver}/libseccomp-#{ver}.tar.gz" },
  fetch_via_git: false,
)

#
# host_libseccomp: the syscall-filter library, needed by glycin.
#
# glycin does not merely decode images, it decodes them in a sandbox:
# each format is handled by a separate process confined with seccomp,
# on the reasoning that image parsers are where untrusted input meets
# C. libglycin/meson.build requires libseccomp >= 2.5.0 on Linux and
# will not configure without it.
#
# Pure C with no dependencies of its own, so it is an ordinary leaf
# here even though what sits on top of it is not.
#
class HostLibseccompPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_libseccomp',
      source: LIBSECCOMP_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gcc', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HOST_STACK_ENABLED
  def pkg_dirname = "libseccomp"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libseccomp.so", false],
    ["install/usr/include/seccomp.h", false],
    ["install/usr/lib/pkgconfig/libseccomp.pc", false],
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
    return autotools_stack_build(install_dir, args: [
      "--disable-static",
      "--libdir=#{stack_sysroot}/usr/lib",
      "--disable-python",   # the bindings are not wanted, only the C lib
    ])
  end
end

pkgmgr.register(HostLibseccompPackage.new())
