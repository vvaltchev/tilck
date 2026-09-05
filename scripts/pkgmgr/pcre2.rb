# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

PCRE2_SOURCE = SourceRef.new(
  name: 'pcre2',
  url:  GITHUB + '/PCRE2Project/pcre2/releases/download',
  tarname: ->(ver) { "pcre2-#{ver}.tar.bz2" },
  remote_tarname: ->(ver) { "pcre2-#{ver}/pcre2-#{ver}.tar.bz2" },

  # Stated rather than left to the auto-detection, which looks for
  # "/releases/download/" with a trailing slash and would take this
  # URL for a repo root and try to clone it.
  fetch_via_git: false,
)

#
# host_pcre2: the regular expression engine glib's GRegex is built on.
# glib has required PCRE2 rather than the original PCRE since 2.74.
#
class HostPcre2Package < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_pcre2',
      source: PCRE2_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gcc', true), Dep('host_zlib', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

  def expected_files(ver = nil) = [
    ["install/usr/lib/libpcre2-8.so", false],
    ["install/usr/include/pcre2.h", false],
    ["install/usr/lib/pkgconfig/libpcre2-8.pc", false],
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
    super(dir)
  end

  def build_flags(ver = nil) = [
      "--disable-static",
      "--enable-jit",           # glib builds GRegex against the JIT
      "--libdir=#{stack_sysroot}/usr/lib",
  ]

  def install_impl_internal(install_dir)

    return autotools_stack_build(install_dir)
  end
end

pkgmgr.register(HostPcre2Package.new())
