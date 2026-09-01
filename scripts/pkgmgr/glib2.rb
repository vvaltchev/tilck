# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

GLIB2_SOURCE = SourceRef.new(
  name: 'glib2',
  url:  'https://download.gnome.org/sources/glib',
  tarname: ->(ver) { "glib-#{ver}.tar.xz" },

  # GNOME publishes each release under its <major>.<minor>/ series.
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/glib-#{ver}.tar.xz"
  },
)

#
# host_glib2: the library QEMU is built on, and the root of the GTK
# closure — everything above it (pango, cairo, gdk-pixbuf, gtk) depends
# on it.
#
# QEMU 6.2 requires glib >= 2.56 and current QEMU rather more, so one
# recent glib serves every major we intend to support. That is why the
# closure is not version-specific even though QEMU is.
#
# Several subsystems are switched off deliberately, each of which would
# otherwise drag in a dependency that nothing in a QEMU build needs:
#
#   nls           gettext, for translations of glib's own messages
#   libmount      util-linux, for GUnixMountMonitor
#   selinux       libselinux
#   introspection gobject-introspection, and a Python stack with it
#   man-pages     docbook and xsltproc
#
# Translations are the one that is a real choice rather than an obvious
# omission: disabling NLS here means glib's own diagnostics are English.
# GTK's UI translations are a separate question, deferred to when GTK
# lands, and reversing this costs one gettext package.
#
class HostGlib2Package < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_glib2',
      source: GLIB2_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_zlib', true),
        Dep('host_libffi', true),
        Dep('host_pcre2', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HOST_STACK_ENABLED

  def expected_files(ver = nil) = [
    ["install/usr/lib/libglib-2.0.so", false],
    ["install/usr/lib/libgobject-2.0.so", false],
    ["install/usr/lib/libgio-2.0.so", false],
    ["install/usr/include/glib-2.0/glib.h", false],
    ["install/usr/lib/pkgconfig/glib-2.0.pc", false],
  ]

  # glib ships code generators — glib-compile-resources, gdbus-codegen,
  # glib-compile-schemas — that packages above it invoke by name during
  # their own builds, so the bin directory is published alongside the
  # headers and libraries.
  def build_env(ver)

    prefix = install_prefix(ver) / "install" / "usr"

    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "glib-2.0",
                        prefix / "lib" / "glib-2.0" / "include"],
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

  def build_flags(ver = nil) = [
      "-Dnls=disabled",
      "-Dlibmount=disabled",
      "-Dselinux=disabled",
      "-Dintrospection=disabled",
      "-Dman-pages=disabled",
      "-Dtests=false",
  ]

  def install_impl_internal(install_dir)

    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostGlib2Package.new())
