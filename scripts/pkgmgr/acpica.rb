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
      name: 'acpica',
      url: GITHUB + '/acpica/acpica',
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # nil => noarch package
      dep_list: []
    )
  end

  def expected_files = [
    ["3rd_party", true],
    ["Makefile", false],
    ["source", true],
    ["source/components/namespace", true],
  ]

  def install_impl_internal(ignored = nil)
    apply_code_patches()
    chdir!("3rd_party") {
      File.write("README", "Directory created by Tilck")
      ln_s("../source/include", "acpi")
    }
    return true
  end

  def default_arch = nil
  def default_cc = nil

  private
  # In-code patch applier: string substitutions in source files. Separate
  # from the base class `apply_patches(ver)` which consumes diff files
  # under scripts/patches/<pkg>/<ver>/.
  def apply_code_patches
    for filepath, patches in PATCHES
      s = File.read(filepath)
      patches.each { |before, after| s = s.gsub(before, after) }
      File.write(filepath, s)
    end
  end
end

pkgmgr.register(Acpica.new())
