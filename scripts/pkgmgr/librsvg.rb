# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'system_deps'
require_relative 'cargo_build'

LIBRSVG_SOURCE = SourceRef.new(
  name: 'librsvg',
  url:  'https://download.gnome.org/sources/librsvg',
  tarname: ->(ver) { "librsvg-#{ver}.tar.xz" },
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/librsvg-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# host_librsvg: SVG rendering, and the second Rust package in the tree.
#
# Here for the icons. A GTK icon theme is mostly SVG — Adwaita ships
# 555 of them against 1331 PNGs, and every "symbolic" icon is one — so
# without an SVG decoder GTK logs
#
#   Gtk-WARNING: Could not load a pixbuf from icon theme
#
# and draws nothing where those icons belong. glycin-svg is the loader
# that closes that, and librsvg is what it renders with.
#
# gdk-pixbuf support is off entirely, and that is not just trimming:
# leaving it at its default `auto` creates a dependency CYCLE, which
# the graph check refuses before anything builds --
#
#   host_gdk_pixbuf -> host_glycin -> host_librsvg -> host_gdk_pixbuf
#
# because gdk-pixbuf decodes through glycin, glycin renders SVG
# through librsvg, and librsvg would then want gdk-pixbuf back. It is
# a real cycle rather than a bookkeeping one, and the way out is that
# the middle link is unnecessary: glycin-svg renders through
# librsvg's CAIRO API, so nothing here needs librsvg to hand back a
# GdkPixbuf. The loader module it can install is off for the same
# reason -- modules are not consulted when a loader is builtin.
#
# AVIF is off too, which is what keeps dav1d out of the closure: an
# AV1 decoder, for AVIF images embedded inside SVGs, is a lot of
# machinery for something an icon theme does not contain.
#
class HostLibrsvgPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts
  include CargoBuild

  def initialize
    super(
      name: 'host_librsvg',
      source: LIBRSVG_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_cairo', true),
        Dep('host_pango', true),
        Dep('host_freetype', true),
        Dep('host_harfbuzz', true),
        Dep('host_libxml2', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HOST_STACK_ENABLED
  def pkg_dirname = "librsvg"

  # Plus cargo-c: librsvg's meson.build looks for the `cargo-cbuild`
  # program by name (meson.build:28) and refuses to configure without
  # it. It is what turns the Rust crate into a C library, header and
  # .pc file -- which is the only form anything here can consume.
  def system_deps(ver = nil)
    return rust_system_deps + [SystemDeps::CARGO_C]
  end

  def expected_files(ver = nil) = [
    ["install/usr/lib/librsvg-2.so", false],
    ["install/usr/lib/pkgconfig/librsvg-2.0.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "librsvg-2.0"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  # Unlike glycin, librsvg needs no cross file: it takes the cargo
  # target from its own `triplet` option. Everything else about
  # driving cargo is the same, and lives in CargoBuild.
  def install_impl_internal(install_dir)
    with_cargo_env do
      meson_stack_build(install_dir, args: [
        "-Dtriplet=#{cargo_triple}",
        "-Davif=disabled",
        "-Dpixbuf=disabled",
        "-Dintrospection=disabled",
        "-Dvala=disabled",
        "-Ddocs=disabled",
        "-Dpixbuf-loader=disabled",
        "-Dtests=false",
      ])
    end
  end
end

pkgmgr.register(HostLibrsvgPackage.new())
