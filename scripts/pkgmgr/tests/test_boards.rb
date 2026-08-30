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

  def pkg = FakePackage.new("boardpkg", arch_list: [RV])

  def test_an_install_for_one_board_is_not_installed_for_the_other
    with_fake_tc do
      fake_install("qemu-virt")

      with_context(ARCH: RV, BOARD: "qemu-virt") do
        assert pkg.installed?(VER), "the board it was built for"
      end

      with_context(ARCH: RV, BOARD: "licheerv-nano") do
        refute pkg.installed?(VER), "another board must not inherit it"
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
          path = pkg.install_prefix(VER)
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
        assert pkg.installed?(VER)
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
end
