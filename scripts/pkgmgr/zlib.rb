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

  PROJ_NAME = 'zlib'
  URL = GITHUB + '/madler/' + PROJ_NAME
  CURR_VER = pkgmgr.get_config_ver(PROJ_NAME).to_s

  def initialize
    super(
      name: PROJ_NAME,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def install_impl(ver = nil)
    raise "not implemented"
  end

end

pkgmgr.register(ZlibPackage.new())
