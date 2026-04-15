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

  def expected_files = [
    ["install/lib/libz.a", false]
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def install_impl_internal(install_dir)

    arch = default_arch().gcc_tc
    ok = run_command("configure.log", [
      "./configure",
      "--prefix=#{install_dir}/install",
      "--static"
    ])
    return false if !ok

    ok = run_command("build.log", [
      "make",
      "-j#{BUILD_PAR}",
      "AR=#{arch}-linux-ar",
      "ARFLAGS=rcs",
      "RANLIB=#{arch}-linux-ranlib",
    ])
    return false if !ok

    ok = run_command("install.log", [ "make", "install" ])
    return false if !ok

    return true
  end
end

pkgmgr.register(ZlibPackage.new())
