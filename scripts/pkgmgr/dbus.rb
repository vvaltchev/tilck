# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

DBUS_SOURCE = SourceRef.new(
  name: 'dbus',
  url:  'https://dbus.freedesktop.org/releases/dbus',
  tarname: ->(ver) { "dbus-#{ver}.tar.xz" },
  fetch_via_git: false,
)

#
# host_dbus: the IPC library at-spi2-core is built on — the
# accessibility bus is a D-Bus bus, and libdbus is what talks to it.
#
# Only the library is wanted. The system and session daemons, the
# launcher and the traditional /var/lib/dbus machine-id all belong to
# a running desktop, not to a toolchain: QEMU links libdbus through
# at-spi2-core and connects to whatever bus the user's session already
# provides, or to none.
#
class HostDbusPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_dbus',
      source: DBUS_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_expat', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def pkg_dirname = "dbus"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libdbus-1.so", false],
    ["install/usr/lib/pkgconfig/dbus-1.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "dbus-1.0"],
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
      "-Ddoxygen_docs=disabled",
      "-Dducktype_docs=disabled",
      "-Dxml_docs=disabled",
      "-Dsystemd=disabled",
      "-Dselinux=disabled",
      "-Dapparmor=disabled",
      "-Dmodular_tests=disabled",
  ]

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostDbusPackage.new())
