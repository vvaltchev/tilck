# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class ZlibPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'zlib',
      url: GITHUB + '/madler/zlib',
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def install_impl_internal(install_subdir)
    ok = run_command("configure.log", [
      "./configure",
      "--prefix=#{install_subdir}",
      "--static"
    ])
    return false if !ok

    ok = run_command("build.log", [ "make", "-j#{BUILD_PAR}" ])
    return false if !ok

    ok = run_command("install.log", [ "make", "install" ])
    return false if !ok

    return true
  end
end

pkgmgr.register(ZlibPackage.new())
