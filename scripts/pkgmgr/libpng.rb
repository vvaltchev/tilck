# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LIBPNG_SOURCE = SourceRef.new(
  name: 'libpng',
  url:  'https://download.sourceforge.net/libpng',
  tarname: ->(ver) { "libpng-#{ver}.tar.xz" },
  fetch_via_git: false,
)

#
# host_libpng: PNG decoding, needed by cairo for image surfaces and by
# gdk-pixbuf for icons. Depends only on zlib.
#
class HostLibpngPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_libpng',
      source: LIBPNG_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gcc', true), Dep('host_zlib', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libpng16.so", false],
    ["install/usr/include/libpng16/png.h", false],
    ["install/usr/lib/pkgconfig/libpng16.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include", prefix / "include" / "libpng16"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def build_flags(ver = nil) = [
      "--disable-static",
      "--libdir=#{stack_sysroot}/usr/lib",
  ]

  def install_impl_internal(install_dir)
    return autotools_stack_build(install_dir)
  end
end

pkgmgr.register(HostLibpngPackage.new())
