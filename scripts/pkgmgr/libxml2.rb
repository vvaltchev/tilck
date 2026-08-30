# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LIBXML2_SOURCE = SourceRef.new(
  name: 'libxml2',
  url:  'https://download.gnome.org/sources/libxml2',
  tarname: ->(ver) { "libxml2-#{ver}.tar.xz" },
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/libxml2-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# host_libxml2: XML parsing, pulled in by at-spi2-core, which reads
# the accessibility bus's configuration with it.
#
# Built as a bare parser. The Python bindings and ICU need
# dependencies we do not have, and the HTTP client is network code
# compiled into a library that is here only to read a local config
# file. 2.15 has no lzma option at all — the compression support it
# does have comes through zlib.
#
class HostLibxml2Package < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_libxml2',
      source: LIBXML2_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_zlib', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HOST_STACK_ENABLED
  def pkg_dirname = "libxml2"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libxml2.so", false],
    ["install/usr/lib/pkgconfig/libxml-2.0.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "libxml2"],
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
    return meson_stack_build(install_dir, args: [
      "-Dpython=disabled",
      "-Dhttp=disabled",
      "-Ddocs=disabled",
      "-Dicu=disabled",
    ])
  end
end

pkgmgr.register(HostLibxml2Package.new())
