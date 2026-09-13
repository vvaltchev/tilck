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

  def expected_files(ver = nil) = [
    ["include", true],
    ["src",     true],
  ]

  # Nothing is built: the kernel compiles these sources itself, and
  # the install only has to leave a link where the build expects one.
  # Symlink replaces a link and refuses anything else, which is the
  # guard the old rm_f-if-symlink spelled out.
  #
  # $FINAL, not the directory the build is standing in. That one is
  # the STAGING tree, and the atomic move takes it away a moment
  # later -- so the link this package exists to create has been
  # dangling ever since staging was introduced. Nothing noticed
  # because the userapp that needs it is EXTRA_* and off by default.
  def build_steps(ver = nil) = [
    Symlink(target: "$FINAL", link: "$SRC/userapps/extra/tfblib"),
  ]

  def default_arch = nil
  def default_cc = nil
end

pkgmgr.register(TfblibPackage.new())
