# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class BusyBoxPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'busybox',
      url: "https://busybox.net/downloads",
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def tarname(ver) = "#{name}-#{ver}.tar.bz2"

  def expected_files = [
    ["busybox", false],
  ]

  def install_impl_internal(install_dir)
    cp(MAIN_DIR / "other" / "busybox.config", ".config")
    return run_command("build.log", [ "make", "V=1", "-j#{BUILD_PAR}" ])
  end
end

pkgmgr.register(BusyBoxPackage.new())
