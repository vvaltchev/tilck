# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

FREETYPE_SOURCE = SourceRef.new(
  name: 'freetype',
  url:  'https://download.savannah.gnu.org/releases/freetype',
  tarname: ->(ver) { "freetype-#{ver}.tar.xz" },
  fetch_via_git: false,
)

#
# host_freetype: font rasterisation, underneath fontconfig, harfbuzz,
# cairo and pango — most of the text half of the GTK closure.
#
# freetype and harfbuzz depend on each other upstream: harfbuzz uses
# freetype to read font tables, and freetype can use harfbuzz to
# improve auto-hinting. The cycle is broken the way every distro breaks
# it — build freetype WITHOUT harfbuzz first. The auto-hinter is
# slightly worse for it, which nothing here can tell.
#
class HostFreetypePackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_freetype',
      source: FREETYPE_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_zlib', true),
        Dep('host_libpng', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libfreetype.so", false],
    ["install/usr/include/freetype2/ft2build.h", false],
    ["install/usr/lib/pkgconfig/freetype2.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "freetype2"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  def build_flags(ver = nil) = [
      "-Dharfbuzz=disabled",   # see the cycle note above
      "-Dbrotli=disabled",
      "-Dbzip2=disabled",
  ]

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostFreetypePackage.new())
