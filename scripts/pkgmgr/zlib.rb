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

  def initialize
    super(
      name: PROJ_NAME,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def install_impl(ver)
    ver = ver.to_s()
    ok = Cache::download_git_repo(URL, tarname, ver)
    return false if !ok

    pkgmgr.with_cc() do |arch_dir|
      chdir_package_base_dir(arch_dir) do

        ok = Cache::extract_file(TC_CACHE / tarname)
        return false if !ok

        ok = chdir_install_dir(arch_dir, ver) do
          system("echo aaa > bbb")
        end

        return false if !ok
      end
    end

    return true
 end

end

pkgmgr.register(ZlibPackage.new())
