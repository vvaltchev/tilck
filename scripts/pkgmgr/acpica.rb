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
  PATCHES = {
    'source/include/platform/acenv.h' => {
      '#if defined(_LINUX) || defined(__linux__)' =>
        '#if defined(__TILCK_KERNEL__)   // patched',

      '#include "aclinux.h"' =>
        '#include "tilck/acpi/actilck.h" // patched',
    },
  }

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
    raise LocalError, "Couldn't download file" if !ok

    mkdir_p(TC_NOARCH / name)
    chdir(TC_NOARCH / name) do
      Cache::extract_file(TC_CACHE / TARNAME)
      contents = Dir.children(".")
      if contents.length != 1 or contents[0] != CURR_VER
        error "Extracted archive does not match expectations: #{TARNAME}"
        return false
      end

      chdir(CURR_VER) {
        apply_patches()
        chdir!("3rd_party") {
          File.write("README", "Directory created by Tilck")
          ln_s("../source/include", "acpi")
        }
      }
    end

    return true

  rescue LocalError => e
    error e
    return false
  end

  private
  def apply_patches
    for filepath, patches in PATCHES
      s = File.read(filepath)
      patches.each { |before, after| s = s.gsub(before, after) }
      File.write(filepath, s)
    end
  end
end

pkgmgr.register(Acpica.new())
