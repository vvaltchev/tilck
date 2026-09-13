# SPDX-License-Identifier: BSD-2-Clause
#
# The system tests' own arithmetic: which targets a run is about, and
# which CMake flags a build gets. The lane itself costs hours; these
# are the parts of it that can be wrong in a second.

require_relative 'test_helper'
require_relative 'system_tests'

class TestSystemTestsTargets < Minitest::Test

  RV = ALL_ARCHS["riscv64"]

  def targets(arch, board) = SystemTests.resolve_targets(arch, board)

  # fail! says why and exits: the reason, as printed.
  def refused(arch, board)
    _, err = capture_io {
      assert_raises(SystemExit) { targets(arch, board) }
    }
    return err
  end

  def test_one_arch_is_its_default_board
    assert_equal [["riscv64", "qemu-virt"]], targets("riscv64", nil)
    assert_equal [["i386", "pc"]], targets("i386", nil)
  end

  def test_b_ALL_is_every_board_of_each_arch
    assert_equal RV.boards.map { |b| ["riscv64", b] }, targets("riscv64", "ALL")

    all = targets("ALL", "ALL")
    assert_equal SystemTests::ALL_TEST_ARCHS.sum { |a|
                   ALL_ARCHS[a].boards.length
                 }, all.length
    assert_includes all, ["riscv64", "licheerv-nano"]
    assert_includes all, ["x86_64", "pc"]
  end

  def test_a_named_board_is_that_board
    assert_equal [["riscv64", "licheerv-nano"]],
                 targets("riscv64", "licheerv-nano")
  end

  def test_a_board_belongs_to_one_arch
    assert_match(/-b pc names one arch's board/, refused("ALL", "pc"))
    assert_match(/Unknown board licheerv-nano for i386/,
                 refused("i386", "licheerv-nano"))
  end
end

class TestSystemTestsExtraFlags < Minitest::Test

  # A flag is passed for the optional package the target got, by the
  # package's name -- never by a path into the toolchain, which is
  # how every flag silently stopped being passed when the layout
  # changed.
  def test_flags_follow_what_was_installed
    flags = SystemTests.extra_cmake_flags(%w[busybox vim tfblib])
    assert_equal ["-DEXTRA_VIM=1", "-DEXTRA_TFBLIB=1"], flags
    assert_empty SystemTests.extra_cmake_flags(%w[busybox])
  end

  def test_every_flag_names_a_registered_package
    names = REAL_PACKAGES.map(&:name)

    SystemTests::EXTRA_FLAG_MAP.each_value { |pkg|
      assert_includes names, pkg
    }
  end
end
