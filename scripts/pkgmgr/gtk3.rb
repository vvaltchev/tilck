# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

GTK3_SOURCE = SourceRef.new(
  name: 'gtk3',
  url:  'https://download.gnome.org/sources/gtk',
  tarname: ->(ver) { "gtk-#{ver}.tar.xz" },
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/gtk-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# host_gtk3: the toolkit QEMU draws its window, menus and dialogs with.
# The top of the closure, and the reason almost everything below it is
# here.
#
# GTK 3 rather than 4 because that is what QEMU 6.2 targets; its
# ui/gtk.c is written against the GTK 3 API.
#
# Backends: X11 only. Wayland would add wayland-client, wayland-cursor,
# wayland-egl, wayland-protocols and a scanner to the closure, and
# QEMU under X11 is what this stack is built to run. Broadway (the
# HTML5 backend) is off by default upstream and stays off.
#
# Everything else turned off here is either a dependency we decline or
# a build artefact nobody consumes:
#
#   * cloudproviders, tracker3, colord — optional integrations, each
#     pulling a service dependency in for a feature QEMU never calls;
#   * introspection — needs gobject-introspection, and nothing here
#     binds GTK from a scripting language;
#   * print_backends=file — the alternative is CUPS;
#   * demos, examples, tests, gtk_doc, man — not shipped.
#
class HostGtk3Package < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_gtk3',
      source: GTK3_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_cairo', true),
        Dep('host_pango', true),
        Dep('host_at_spi2_core', true),
        Dep('host_gdk_pixbuf', true),
        Dep('host_libepoxy', true),
        Dep('host_xkbcommon', true),
        Dep('host_fribidi', true),
        Dep('host_harfbuzz', true),
        Dep('host_fontconfig', true),
        Dep('host_libx11', true),
        Dep('host_libxext', true),
        Dep('host_libxrender', true),
        Dep('host_libxfixes', true),
        Dep('host_libxi', true),
        Dep('host_libxrandr', true),
        Dep('host_libxcursor', true),
        Dep('host_libxinerama', true),
        Dep('host_libxdamage', true),
        Dep('host_libxcomposite', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED
  def pkg_dirname = "gtk3"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libgtk-3.so", false],
    ["install/usr/lib/libgdk-3.so", false],
    ["install/usr/lib/pkgconfig/gtk+-3.0.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "gtk-3.0"],
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
      "-Dx11_backend=true",
      "-Dwayland_backend=false",
      "-Dbroadway_backend=false",
      "-Dcloudproviders=false",
      "-Dprofiler=false",
      "-Dtracker3=false",
      "-Dcolord=no",
      "-Dintrospection=false",
      "-Dprint_backends=file",
      "-Dgtk_doc=false",
      "-Dman=false",
      "-Ddemos=false",
      "-Dexamples=false",
      "-Dtests=false",
    ])
  end
end

pkgmgr.register(HostGtk3Package.new())
