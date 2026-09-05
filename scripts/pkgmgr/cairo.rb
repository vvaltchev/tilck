# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

CAIRO_SOURCE = SourceRef.new(
  name: 'cairo',
  url:  'https://www.cairographics.org/releases',
  tarname: ->(ver) { "cairo-#{ver}.tar.xz" },
  fetch_via_git: false,
)

#
# host_cairo: 2D rendering. GTK draws everything through it, and it is
# where the text stack and the X11 stack meet — it needs freetype and
# fontconfig on one side and libX11/libXrender on the other.
#
class HostCairoPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_cairo',
      source: CAIRO_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_pixman', true),
        Dep('host_freetype', true),
        Dep('host_fontconfig', true),
        Dep('host_libpng', true),
        Dep('host_zlib', true),
        Dep('host_glib2', true),
        Dep('host_libxext', true),
        Dep('host_libxrender', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

  def expected_files(ver = nil) = [
    ["install/usr/lib/libcairo.so", false],
    ["install/usr/include/cairo/cairo.h", false],
    ["install/usr/lib/pkgconfig/cairo.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "cairo"],
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
      "-Dxlib=enabled",
      "-Dfreetype=enabled",
      "-Dfontconfig=enabled",
      "-Dpng=enabled",
      "-Dzlib=enabled",
      "-Dtests=disabled",
      "-Dspectre=disabled",
      # cairo-gobject, which GTK links against directly. An
      # earlier version of this disabled glib on the assumption
      # that it only affected cairo's own tests; it does not.
      "-Dglib=enabled",
  ]

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostCairoPackage.new())
