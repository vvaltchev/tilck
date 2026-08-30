# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

FRIBIDI_SOURCE = SourceRef.new(
  name: 'fribidi',
  url:  GITHUB + '/fribidi/fribidi/releases/download',
  tarname: ->(ver) { "fribidi-#{ver}.tar.xz" },
  remote_tarname: ->(ver) { "v#{ver}/fribidi-#{ver}.tar.xz" },
  fetch_via_git: false,
)

#
# host_fribidi: the Unicode bidirectional algorithm, which pango needs
# to lay out mixed left-to-right and right-to-left text. Another leaf.
#
class HostFribidiPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_fribidi',
      source: FRIBIDI_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gcc', true), Dep('host_meson', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

  def expected_files(ver = nil) = [
    ["install/usr/lib/libfribidi.so", false],
    ["install/usr/include/fribidi/fribidi.h", false],
    ["install/usr/lib/pkgconfig/fribidi.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "fribidi"],
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
      "-Ddocs=false",
      "-Dbin=false",     # the CLI tool is not wanted, only the library
    ])
  end
end

pkgmgr.register(HostFribidiPackage.new())
