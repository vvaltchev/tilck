# SPDX-License-Identifier: BSD-2-Clause
#
# The layout, as seen from outside the package manager.
#
# CMake needs to know where installed packages live. It used to build
# those paths itself, which meant the schema existed twice: once in
# Coords and once, spelled out, in CMakeLists.txt. The two drifted the
# moment the layout changed -- the CMake half still described
# toolchain4 after everything else had moved on, and a build against
# the new tree looked for directories nothing had created.
#
# So CMake asks instead. This prints KEY=value lines; Coords stays the
# only thing that knows what a path looks like, and the next layout
# change is one edit rather than a hunt for everyone who guessed.
#
# Emitted keys:
#
#   ARCH, BOARD, HOST_DISTRO, HOST_CC
#       The coordinates the answers were computed from. CMake derives
#       these too, so it can compare and stop rather than build half
#       the tree against one architecture and half against another.
#
#   PKGS_HOST_PORTABLE, PKGS_HOST_DISTRO, PKGS_HOST_CC, PKGS_NOARCH
#       The three host tiers and the noarch tree.
#
#   PKGS_TARGET
#       Packages for the arch and board being built right now.
#
#   PKGS_TARGET_<arch>
#       Packages for one specific arch, at its own default board. The
#       x86 UEFI bootloader needs this: an x86_64 build has to link
#       the ia32 loader against the i386 gnuefi.
#

require_relative 'early_logic'
require_relative 'arch'
require_relative 'coords'

module Layout

  module_function

  # The board an arch's packages are under: the one asked for when it
  # is the arch being built, otherwise that arch's own default. Same
  # rule as Package#target_board, and for the same reason -- BOARD
  # names a board of ARCH, and means nothing for any other arch.
  def board_of(arch)
    return BOARD if arch == ARCH && BOARD
    return arch.default_board
  end

  def target_pkgs(arch)
    return nil if arch.gcc_ver.nil?
    return Coords.new("tilck-#{arch.name}", board_of(arch),
                      "gcc-#{arch.gcc_ver}").pkgs_dir
  end

  def vars

    v = {
      "ARCH"        => ARCH.name,
      "BOARD"       => BOARD,
      "HOST_DISTRO" => HOST_DISTRO,
      "HOST_CC"     => HOST_CC,
      "TCROOT"      => TC,

      "PKGS_HOST_PORTABLE" => Coords.new(HOST_OS_ARCH, nil, nil).pkgs_dir,
      "PKGS_HOST_DISTRO"   => Coords.new(HOST_OS_ARCH, HOST_DISTRO,
                                         nil).pkgs_dir,
      "PKGS_HOST_CC"       => Coords.new(HOST_OS_ARCH, HOST_DISTRO,
                                         HOST_CC).pkgs_dir,
      "PKGS_NOARCH"        => Coords.new("noarch", nil, nil).pkgs_dir,
      "PKGS_TARGET"        => target_pkgs(ARCH),
    }

    # An arch with no compiler version configured has no package tree
    # to name, so it is left out rather than emitted as a path with a
    # hole in it. A consumer that needs one and does not find it can
    # say so; a consumer handed "gcc-" cannot.
    for arch in ALL_ARCHS.values
      p = target_pkgs(arch)
      v["PKGS_TARGET_#{arch.name}"] = p if p
    end

    return v
  end

  def print_vars
    vars.each { |k, val| puts "#{k}=#{val}" }
  end
end
