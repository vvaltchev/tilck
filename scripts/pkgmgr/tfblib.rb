# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

TFBLIB_SOURCE = SourceRef.new(
  name: 'tfblib',
  url:  GITHUB + '/vvaltchev/tfblib',
)

class TfblibPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  SYMLINK_DEST = MAIN_DIR / "userapps" / "extra" / "tfblib"

  def initialize
    super(
      name: 'tfblib',
      source: TFBLIB_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # noarch (source-only library)
      dep_list: []
    )
  end

  def expected_files = [
    ["include", true],
    ["src",     true],
  ]

  def install_impl_internal(ignored = nil)
    install_dir = mkpathname(getwd)
    rm_f(SYMLINK_DEST) if SYMLINK_DEST.symlink?
    ln_s(install_dir, SYMLINK_DEST)
    return true
  end

  def default_arch = nil
  def default_cc = nil
end

pkgmgr.register(TfblibPackage.new())
