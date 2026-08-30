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
# glycin is off because it is written in Rust, which this closure
# deliberately excludes. gdk-pixbuf 2.44 defaults it to enabled on
# Linux and it becomes the ONLY builtin loader when present, so
# disabling it also means naming the loaders we want explicitly —
# otherwise 'default' resolves to glycin alone and nothing can decode
# a PNG.
#
# WITH_GLYCIN below is the switch, and it drives BOTH the meson option
# and the Rust requirement declared in system_deps. Keeping the two
# together is the whole point: a flag flipped without the requirement
# following it produces a build that runs for a while and then fails
# inside cargo, on a machine whose Rust is missing or too old.
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

  # Rust is not in the hermetic closure and we do not build it: when
  # this is turned on, rustc and cargo have to come from the host.
  WITH_GLYCIN = false

  def initialize
    super(
      name: 'host_gdk_pixbuf',
      source: GDK_PIXBUF_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_libpng', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED
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
    return meson_hermetic_build(install_dir, args: [
      "-Dpng=enabled",
      "-Djpeg=disabled",
      "-Dtiff=disabled",
      "-Dintrospection=disabled",
      "-Dman=false",
      "-Dtests=false",
      "-Dgio_sniffing=false",
      "-Dglycin=#{WITH_GLYCIN ? "enabled" : "disabled"}",
      "-Dbuiltin_loaders=png",
    ])
  end
end

pkgmgr.register(HostGdkPixbufPackage.new())
