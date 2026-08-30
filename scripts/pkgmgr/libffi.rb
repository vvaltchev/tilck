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
# autotools package of the hermetic stack.
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
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gcc', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

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

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def install_impl_internal(install_dir)

    # The git tarball has no configure script; autogen builds one.
    ok = run_command("autogen.log", ["./autogen.sh"])
    return false if !ok

    return autotools_hermetic_build(install_dir, args: [
      "--disable-static",       # the sysroot ships shared libraries
      "--libdir=#{hermetic_sysroot}/usr/lib",
      "--disable-multi-os-directory",  # keeps it out of lib64
    ])
  end
end

pkgmgr.register(HostLibffiPackage.new())
