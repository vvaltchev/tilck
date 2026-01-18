# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class Acpica < Package

  include FileShortcuts
  include FileUtilsShortcuts

  PROJ_NAME = 'acpica'
  URL = GITHUB + '/acpica/' + PROJ_NAME
  CURR_VER = pkgmgr.get_config_ver(PROJ_NAME).to_s
  TARNAME = "#{PROJ_NAME}-#{CURR_VER}.tgz"

  def initialize
    super(
      name: PROJ_NAME,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # nil => noarch package
      dep_list: []
    )
  end

  def install_impl(ver = nil)
    ok = Cache::download_git_repo(URL, TARNAME, CURR_VER, CURR_VER)
    raise "Couldn't download file" if !ok

    mkdir_p(TC_NOARCH / name)
    chdir(TC_NOARCH / name) do
      Cache::extract_file(TC_CACHE / TARNAME)
      contents = Dir.children(".")
      if contents.length != 1 or contents[0] != CURR_VER
        error "Extracted archive does not match expectations: #{TARNAME}"
        return false
      end
    end

    return true

  rescue LocalError => e
    error e
    return false
  end

end

pkgmgr.register(Acpica.new())
