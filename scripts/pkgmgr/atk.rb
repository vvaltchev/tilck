# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

ATK_SOURCE = SourceRef.new(
  name: 'atk',
  url:  'https://download.gnome.org/sources/atk',
  tarname: ->(ver) { "atk-#{ver}.tar.xz" },
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/atk-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# host_atk: the accessibility interfaces GTK 3 exposes its widgets
# through. A hard dependency of GTK 3 whether or not a screen reader is
# ever attached — the toolkit implements AtkObject throughout.
#
# Only the interface library is built. The bridge that carries those
# interfaces onto the accessibility bus (at-spi2-atk) is a separate
# project and is not needed to link or run GTK.
#
class HostAtkPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_atk',
      source: ATK_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

  def expected_files(ver = nil) = [
    ["install/usr/lib/libatk-1.0.so", false],
    ["install/usr/lib/pkgconfig/atk.pc", false],
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
      "-Dintrospection=false",
      "-Ddocs=false",
    ])
  end
end

pkgmgr.register(HostAtkPackage.new())
