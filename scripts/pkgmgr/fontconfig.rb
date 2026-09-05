# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

# Upstream stopped publishing to
# freedesktop.org/software/fontconfig/release after 2.16.x; releases
# from 2.17 on are only in the GitLab generic package registry. 890 is
# the fontconfig project id there -- opaque, but it is what the API
# takes, and the human-readable path serves git archives rather than
# the release tarballs.
FONTCONFIG_GITLAB = "https://gitlab.freedesktop.org/api/v4/" \
                    "projects/890/packages/generic/fontconfig"

FONTCONFIG_SOURCE = SourceRef.new(
  name: 'fontconfig',
  url:  FONTCONFIG_GITLAB,
  tarname: ->(ver) { "fontconfig-#{ver}.tar.xz" },
  remote_tarname: ->(ver) { "#{ver}/fontconfig-#{ver}.tar.xz" },
  fetch_via_git: false,
)

#
# host_fontconfig: font discovery and matching — what turns "sans-serif"
# into a file on disk. cairo and pango both need it.
#
# Its own configuration and cache live inside the sysroot rather than
# in /etc, so a QEMU built here does not read the host's font setup.
# It will find whatever fonts the host has through the standard paths
# at RUNTIME, which is correct: fonts are data, not libraries, and
# nothing about them is linked in.
#
class HostFontconfigPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_fontconfig',
      source: FONTCONFIG_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_freetype', true),
        Dep('host_expat', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libfontconfig.so", false],
    ["install/usr/include/fontconfig/fontconfig.h", false],
    ["install/usr/lib/pkgconfig/fontconfig.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include"],
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
      "-Ddoc=disabled",
      "-Dtests=disabled",
      "-Dtools=enabled",       # fc-cache, which gtk wants at runtime
      "-Dnls=disabled",
  ]

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostFontconfigPackage.new())
