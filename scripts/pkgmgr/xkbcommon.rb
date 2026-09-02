# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

XKBCOMMON_SOURCE = SourceRef.new(
  name: 'xkbcommon',
  url:  GITHUB + '/xkbcommon/libxkbcommon/archive/refs/tags',
  tarname: ->(ver) { "libxkbcommon-#{ver}.tar.gz" },
  remote_tarname: ->(ver) { "xkbcommon-#{ver}.tar.gz" },
  fetch_via_git: false,
)

#
# host_xkbcommon: keyboard keymap handling — turning X11 keycodes into
# keysyms according to the user's layout. GTK requires it
# unconditionally on Unix, not only for the Wayland backend.
#
# Three upstream defaults are off, each of which would otherwise pull
# something into the closure for no benefit here:
#
#   * wayland: we build the X11 backend only, and it would need
#     wayland-scanner and the wayland protocol packages;
#   * xkbregistry: parses the layout registry XML, so it wants
#     libxml2 — nothing in QEMU's UI enumerates layouts;
#   * the CLI tools, which are diagnostics for people editing keymaps.
#
class HostXkbcommonPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_xkbcommon',
      source: XKBCOMMON_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_libxcb', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libxkbcommon.so", false],
    ["install/usr/lib/pkgconfig/xkbcommon.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include"],
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
      "-Denable-wayland=false",
      "-Denable-xkbregistry=false",
      "-Denable-docs=false",
      "-Denable-tools=false",
  ]

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostXkbcommonPackage.new())
