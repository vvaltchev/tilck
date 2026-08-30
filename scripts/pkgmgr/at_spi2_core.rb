# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

AT_SPI2_CORE_SOURCE = SourceRef.new(
  name: 'at_spi2_core',
  url:  'https://download.gnome.org/sources/at-spi2-core',
  tarname: ->(ver) { "at-spi2-core-#{ver}.tar.xz" },
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/at-spi2-core-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# host_at_spi2_core: accessibility, and the reason there is no
# standalone atk package here.
#
# GTK 3 requires atk-bridge-2.0 unconditionally — gtk/meson.build:568
# asks for it with no `required:` guard — so this is not an optional
# integration that can be switched off. The bridge is what carries a
# widget's AtkObject onto the accessibility bus.
#
# Upstream merged atk itself into at-spi2-core around 2.51, so this
# one package now provides BOTH atk and atk-bridge-2.0. Building the
# standalone atk 2.38 beside it would put two libatk-1.0.so and two
# atk.pc into the sysroot, which the composer would refuse — correctly.
# So at-spi2-core replaces it rather than joining it.
#
# X11 is ON. The option does not mean what its name suggests: x11_dep
# is consumed only by bus/, registryd/ and atspi/ — the accessibility
# daemon's device-event handling, XTest to synthesise input and
# XInput/XKB to grab keys — and never by atk-adaptor/, which is what
# produces the bridge GTK links. Whether a GTK window appears on X11
# is decided by GTK's own x11_backend. It is enabled because a screen
# reader on an X11 session needs it and the cost is libXtst.
#
class HostAtSpi2CorePackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_at_spi2_core',
      source: AT_SPI2_CORE_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_dbus', true),
        Dep('host_libxml2', true),
        Dep('host_libx11', true),
        Dep('host_libxtst', true),
        Dep('host_libxi', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED
  def pkg_dirname = "at_spi2_core"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libatk-1.0.so", false],
    ["install/usr/lib/libatk-bridge-2.0.so", false],
    ["install/usr/lib/pkgconfig/atk.pc", false],
    ["install/usr/lib/pkgconfig/atk-bridge-2.0.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "atk-1.0"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  def install_impl_internal(install_dir)
    return meson_hermetic_build(install_dir, args: [
      "-Dx11=enabled",
      "-Dintrospection=disabled",
      "-Ddocs=false",
      "-Duse_systemd=false",

      # Defaults to TRUE, and would want gtk+-2.0: the adaptor that
      # bridges GTK 2 widgets is still built by default two major
      # versions later.
      "-Dgtk2_atk_adaptor=false",
    ])
  end
end

pkgmgr.register(HostAtSpi2CorePackage.new())
