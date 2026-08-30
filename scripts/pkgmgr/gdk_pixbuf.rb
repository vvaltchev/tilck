# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'system_deps'

GDK_PIXBUF_SOURCE = SourceRef.new(
  name: 'gdk_pixbuf',
  url:  'https://download.gnome.org/sources/gdk-pixbuf',
  tarname: ->(ver) { "gdk-pixbuf-#{ver}.tar.xz" },
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/gdk-pixbuf-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# host_gdk_pixbuf: image loading for GTK — icons, cursors, anything
# drawn from a file.
#
# JPEG support is off: nothing in QEMU's UI loads a JPEG and PNG covers
# the icon set, so libjpeg-turbo stays out of the closure.
#
# glycin is ON, which makes this the one place in the tree where a
# Rust toolchain is required. WITH_GLYCIN below drives BOTH the meson
# option and that requirement, declared in system_deps: a flag flipped
# without the requirement following it would produce a build that runs
# for a while and then fails inside cargo on a machine whose Rust is
# missing or too old.
#
# builtin_loaders has to name glycin explicitly:
# gdk-pixbuf/meson.build:243 compiles the glycin loader only `if
# builtin_loaders.contains('glycin')`, while the configure summary
# lists it under "Enabled loaders" merely because the dependency was
# found. Leaving it out produced a build that reported
#
#   Enabled loaders  : png
#                      glycin
#
# and shipped no glycin at all: no module, and nothing in
# libgdk_pixbuf's DT_NEEDED.
#
# The summary is equally unreliable in the other direction, so do not
# trust it here. Asking for `png,glycin` reports both as builtin and
# builds only glycin -- the resulting library has 38 undefined gly_*
# symbols and not one png_ symbol or _fill_vtable entry. With glycin
# enabled, gdk-pixbuf decodes through it and nothing else, so there is
# no in-process fallback to be had and asking for one only makes the
# build file lie. builtin_loaders therefore follows the same switch:
# glycin when it is on, png when it is off.
#
# gio_sniffing is off too, and that one is a real trade rather than an
# omission. With it, gdk-pixbuf identifies image formats through GIO's
# MIME database, which means a hard dependency on shared-mime-info and
# libxml2 beneath it — two packages, for identifying files we already
# know the format of. Without it, it falls back to its own magic-byte
# detection, which is what it did for years and is entirely adequate
# for loading a fixed set of PNG icons.
#
class HostGdkPixbufPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  # Rust is not built from source; rustc and cargo come from the host.
  WITH_GLYCIN = true

  def initialize
    super(
      name: 'host_gdk_pixbuf',
      source: GDK_PIXBUF_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_libpng', true),
        Dep('host_libglycin', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HOST_STACK_ENABLED
  def pkg_dirname = "gdk-pixbuf"

  # glycin is a Rust crate: enabling it puts cargo on the critical
  # path of this build. Declared rather than built -- a Rust toolchain
  # is a big thing to compile from source for one image loader, and
  # rustup gives a better one than we would.
  def system_deps(ver = nil)
    return [] if !WITH_GLYCIN
    return [SystemDeps::RUSTC, SystemDeps::CARGO]
  end

  def expected_files(ver = nil) = [
    ["install/usr/lib/libgdk_pixbuf-2.0.so", false],
    ["install/usr/lib/pkgconfig/gdk-pixbuf-2.0.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "gdk-pixbuf-2.0"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
      bin_dirs:        [prefix / "bin"],
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir, args: [
      "-Dpng=enabled",
      "-Djpeg=disabled",
      "-Dtiff=disabled",
      "-Dintrospection=disabled",
      "-Dman=false",
      "-Dtests=false",
      "-Dgio_sniffing=false",
      "-Dglycin=#{WITH_GLYCIN ? "enabled" : "disabled"}",
      "-Dbuiltin_loaders=#{WITH_GLYCIN ? "glycin" : "png"}",
    ])
  end
end

pkgmgr.register(HostGdkPixbufPackage.new())
