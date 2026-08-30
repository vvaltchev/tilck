# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

ZLIB_SOURCE = SourceRef.new(
  name: 'zlib',
  url:  GITHUB + '/madler/zlib',
)

class ZlibPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'zlib',
      source: ZLIB_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: [],
      default: true,
    )
  end

  def expected_files(ver = nil) = [
    ["install/lib/libz.a", false]
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def build_steps

    arch = default_arch().gcc_tc

    return [
      Step("configure.log",
           ["./configure", "--prefix=$INSTALL/install", "--static"]),

      Step("build.log", [
        "make",
        "-j$PAR",
        "AR=#{arch}-linux-ar",
        "ARFLAGS=rcs",
        "RANLIB=#{arch}-linux-ranlib",
      ]),

      Step("install.log", ["make", "install"]),
    ]
  end
end

pkgmgr.register(ZlibPackage.new())
