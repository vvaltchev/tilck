# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

TREECMD_SOURCE = SourceRef.new(
  name: 'treecmd',
  url:  GITHUB + '/vvaltchev/tree-command',
  # The `tilck` branch of the fork, as a commit: a branch moves, and
  # a clone of it after a move could only fail its pin
  # (other/pkg_hashes) with no way back to what the tree was built
  # from. Moving it is editing this line and the pin together.
  git_tag: ->(_ver) { "683188c60ce7b4bf4cb5237e8c053958fcfcc90b" },
)

class TreecmdPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'treecmd',
      source: TREECMD_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def expected_files(ver = nil) = [
    ["tree", false],
  ]

  def clean_build(dir)
    system("make", "clean", chdir: dir.to_s,
           out: "/dev/null", err: "/dev/null")
  end

  def build_steps(ver = default_ver) = [
    Run(log: "build.log", argv: ["make", "-j$PAR"]),
  ]
end

pkgmgr.register(TreecmdPackage.new())
