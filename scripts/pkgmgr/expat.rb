# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

EXPAT_SOURCE = SourceRef.new(
  name: 'expat',
  url:  GITHUB + '/libexpat/libexpat/releases/download',
  tarname: ->(ver) { "expat-#{ver}.tar.xz" },

  # The release directory is the tag (R_2_8_3), the tarball the dotted
  # version.
  remote_tarname: ->(ver) {
    "R_#{ver.to_s.tr(".", "_")}/expat-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# host_expat: the XML parser fontconfig reads its configuration with,
# and a leaf of the GTK closure — it depends on nothing.
#
class HostExpatPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_expat',
      source: EXPAT_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gcc', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

  def expected_files(ver = nil) = [
    ["install/usr/lib/libexpat.so", false],
    ["install/usr/include/expat.h", false],
    ["install/usr/lib/pkgconfig/expat.pc", false],
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

  def install_impl_internal(install_dir)
    return autotools_hermetic_build(install_dir, args: [
      "--disable-static",
      "--without-examples",
      "--without-tests",
      "--libdir=#{hermetic_sysroot}/usr/lib",
    ])
  end
end

pkgmgr.register(HostExpatPackage.new())
