# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'package_manager'

#
# tilck-<arch>-<board>: the Tilck stack of one target, as a package.
#
# A meta-package in APT's sense. It builds nothing and installs an
# empty tree -- a directory with the records every install carries --
# and exists for its dependencies: the cross compiler(s) the target is
# built with and every package declared default for that arch and
# board. The no-mode run installs it, so the default packages come in
# as what they are, dependencies, held by the stack: --autoremove
# leaves them alone while the stack is installed, and removing the
# stack sets them free.
#
# One package per (arch, board), because the default set is per board
# -- u-boot for qemu-virt, its own boot for the LicheeRV Nano -- and a
# board is a coordinate: the install lands at tilck-<arch>/<board>/,
# beside what it holds. The version is the package's own: there is no
# upstream, and what the stack is made of is a matter of dependencies,
# not of versions.
#
class TilckStackPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  VERSION = Ver("1")

  def initialize(arch, board)
    super(
      name: "tilck-#{arch.name}-#{board}",
      source: nil,
      on_host: false,
      arch_list: [arch],
      board_list: [board],
      dep_list: [],
      default: false,
    )
    @arch = arch
    @board = board
  end

  def metapackage? = true
  def default_ver = VERSION
  def installable_versions = [VERSION]
  def expected_files(ver = nil) = []
  def install_impl_internal(install_dir) = true

  # The members: what is declared default for this arch and board, as
  # the packages themselves say it under those coordinates -- the
  # cross compilers included, which on x86 say both of x86's, the UEFI
  # loader being 64-bit whatever the kernel is. A stack is never one:
  # it is declared default by nothing, being what the declaration
  # makes a package a member OF.
  def dep_list
    pkgmgr.with_target_coords(@arch, @board) {
      pkgmgr.all_packages.select(&:default?)
            .map { |p| Dep(p.name, p.on_host) }
    }
  end
end

for arch in ALL_ARCHS.values do
  for board in (arch.boards || []) do
    pkgmgr.register(TilckStackPackage.new(arch, board))
  end
end
