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

  def install_impl(ver)
    ver = ver.to_s()
    ok = Cache::download_git_repo(URL, tarname, ver, ver)
    return false if !ok

    chdir_package_base_dir(nil) do
      ok = Cache::extract_file(TC_CACHE / tarname)
      return false if !ok

      ok = chdir_install_dir(nil, ver) do
        apply_patches()
        chdir!("3rd_party") {
          File.write("README", "Directory created by Tilck")
          ln_s("../source/include", "acpi")
        }
      end
    end

    return true
  end

  def default_arch = nil
  def default_cc = nil

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
