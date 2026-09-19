# SPDX-License-Identifier: BSD-2-Clause
#
# The board is one of the three coordinates of an installation, which
# means it has to be part of deciding whether something IS installed --
# not merely part of the path it is written to.
#
# The bug these cover: find_install matched on (version, compiler,
# arch), a strict subset of the coordinates. Every riscv64 package
# built for qemu-virt therefore reported as installed under
# BOARD=licheerv-nano, and `-s ALL` for that board skipped all of them,
# leaving it with a toolchain of two packages and no way to notice.
#

require_relative 'test_helper'

#
# A coordinate is never blank.
#
# The schema promises exactly three levels. An empty string collapsed
# the path to two -- tilck-i386/gcc-13.3.0 -- which is the toolchain4
# ambiguity Coords exists to remove, and it happened for real: CMake
# passes BOARD= when the user has not chosen a board, Ruby treats ""
# as truthy, so the empty string beat the default and the board level
# simply disappeared from every i386 package path.
#
class TestCoordsRejectBlanks < Minitest::Test

  include TestHelper

  def test_nil_means_any
    c = Coords.new("noarch", nil, nil)
    assert_equal "noarch/any/any", c.to_s
  end

  # Equality is between coordinates. A string that happens to spell
  # the same path is not one, or a filter handed a path would match.
  def test_only_coordinates_are_equal_to_coordinates
    c = Coords.new("tilck-i386", "pc", "gcc-13")
    assert_equal c, Coords.new("tilck-i386", "pc", "gcc-13")
    refute_equal c, c.to_s
    refute_equal c, nil
    refute c.eql?(c.to_s)
  end

  def test_a_blank_env_is_refused
    assert_raises(RuntimeError) { Coords.new("tilck-i386", "", "gcc-13") }
    assert_raises(RuntimeError) { Coords.new("tilck-i386", "  ", "gcc-13") }
  end

  def test_a_blank_machine_or_stack_is_refused
    assert_raises(RuntimeError) { Coords.new("", "pc", "gcc-13") }
    assert_raises(RuntimeError) { Coords.new("tilck-i386", "pc", "") }
  end

  # The whole point of refusing: the path keeps its three levels.
  def test_every_coordinate_has_three_levels
    c = Coords.new("tilck-i386", "pc", "gcc-13.3.0")
    assert_equal 3, c.to_s.split("/").length
  end
end

class TestInstallsAreBoardSpecific < Minitest::Test

  include TestHelper

  # The only arch with more than one board today, which is why it is
  # the only one that could have caught this.
  RV = ALL_ARCHS["riscv64"]
  VER = Ver("1.0.0")

  def fake_install(board)
    FileUtils.mkdir_p(target_pkgs(RV, nil, board) / "boardpkg" / VER.to_s)
  end

  # Registered, because what is installed is what the registry's
  # world says: a package outside it has no installs.
  def pkg
    return @pkg ||= begin
      reset_pkgmgr!
      p = FakePackage.new("boardpkg", arch_list: [RV])
      pkgmgr.register(p)
      p
    end
  end

  def test_an_install_for_one_board_is_not_installed_for_the_other
    with_fake_tc do
      fake_install("qemu-virt")

      with_context(ARCH: RV, BOARD: "qemu-virt") do
        assert bound(pkg).installed?(VER), "the board it was built for"
      end

      with_context(ARCH: RV, BOARD: "licheerv-nano") do
        refute bound(pkg).installed?(VER), "another board must not inherit it"
      end
    end
  end

  # Both boards installed: each has to resolve to its own tree, or a
  # build for one would link against the other's binaries.
  def test_each_board_resolves_to_its_own_install
    with_fake_tc do
      fake_install("qemu-virt")
      fake_install("licheerv-nano")

      for board in RV.boards
        with_context(ARCH: RV, BOARD: board) do
          path = bound(pkg).install_prefix(VER)
          assert_equal target_pkgs(RV, nil, board) / "boardpkg" / VER.to_s,
                       path, "install_prefix must stay on #{board}"
        end
      end
    end
  end

  # The default board is not a special case: it is just the board the
  # environment did not name.
  def test_the_default_board_is_matched_like_any_other
    with_fake_tc do
      fake_install(RV.default_board)

      with_context(ARCH: RV, BOARD: nil) do
        assert bound(pkg).installed?(VER)
      end
    end
  end

  # An install list built for a package with several boards must report
  # every board it found, so that uninstall and status see them all.
  def test_the_install_list_reports_both_boards
    with_fake_tc do
      RV.boards.each { |b| fake_install(b) }

      with_context(ARCH: RV, BOARD: "qemu-virt") do
        found = pkg.get_install_list.map { |x| x.coords.env }.sort
        assert_equal RV.boards.sort, found
      end
    end
  end

  # A package restricted to a board is asked about the board of the
  # arch it would be built FOR, not the one the shell happens to be
  # set to -- the same rule arch_supported? follows for the arch.
  #
  # `-a riscv64 -f -s uboot` from an i386 shell answered "uboot
  # requires board qemu-virt" while BOARD was "pc", refusing to
  # rebuild an install that -f had already removed a second earlier.
  def test_a_board_package_is_reachable_through_the_arch_flag
    p = FakePackage.new("boardpkg", arch_list: [RV],
                        board_list: ["qemu-virt"])

    with_context(ARCH: ALL_ARCHS["i386"], BOARD: "pc") do
      refute bound(p).board_supported?,
             "an i386 shell does not build riscv64 boards by itself"
      assert p.at(scope.with(arch: RV)).board_supported?,
             "-a riscv64 must ask about riscv64's board, not pc"
    end
  end

  # A scope for one installation names its board, and names it only
  # for that install's arch: another arch asked of the same scope
  # still gets its own default, or a board would appear in a path
  # under an arch that has never heard of it. And a scope is a value:
  # the environment's is what it was.
  def test_a_scoped_board_applies_only_to_its_own_arch
    with_context(ARCH: RV, BOARD: "qemu-virt") do
      sc = scope.with(arch: RV, board: "licheerv-nano")
      assert_equal "licheerv-nano", sc.board_of(RV)
      assert_equal "pc", sc.board_of(ALL_ARCHS["i386"])
      assert_equal "qemu-virt", scope.board_of(RV)
    end
  end

  # A package with no arch of its own has no board either, whatever
  # its board_list says: there is no arch for a board to belong to.
  # The guard is what keeps board_for from being asked about nil.
  def test_a_noarch_package_is_bound_to_no_board
    p = FakePackage.new("noarchy", arch_list: nil,
                        board_list: ["qemu-virt"])

    with_context(ARCH: ALL_ARCHS["i386"], BOARD: "pc") do
      assert p.board_supported?, "a package with no arch is not board-bound"
      assert_nil p.board_bsp
    end
  end

  # A host tool with a board_list is a tool for building that board:
  # the SOPHGO toolchain is offered where licheerv-nano is the target,
  # whatever board the arch it runs on has. Asking for the host
  # arch's board gave "pc", which no such list contains.
  def test_a_host_tool_for_a_board_follows_the_target_board
    p = FakePackage.new("host_tool", on_host: true, host_tier: :portable,
                        board_list: ["licheerv-nano"])
    rv = ALL_ARCHS["riscv64"]

    assert p.at(scope.with(arch: rv, board: "licheerv-nano")).board_supported?
    refute p.at(scope.with(arch: rv, board: "qemu-virt")).board_supported?
    refute p.at(scope.with(arch: ALL_ARCHS["i386"])).board_supported?
  end

  # A host package builds for the machine. Boards are a property of
  # what Tilck runs on, so it has no BSP to read.
  def test_a_host_package_has_no_bsp
    assert_nil FakePackage.new("host_thing", on_host: true).board_bsp
  end
end
