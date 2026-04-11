# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class TreecmdPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'treecmd',
      url: GITHUB + '/vvaltchev/tree-command',
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def git_tag(ver) = "tilck"

  def expected_files = [
    ["tree", false],
  ]

  def install_impl_internal(install_dir)
    return run_command("build.log", ["make", "-j#{BUILD_PAR}"])
  end
end

pkgmgr.register(TreecmdPackage.new())
