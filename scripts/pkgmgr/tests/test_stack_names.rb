# SPDX-License-Identifier: BSD-2-Clause
#
# How a stack is spelled, read in both directions.
#
# A stack is "gcc-14.4.0" -- in the path, in the -L listing, in every
# message -- and its grammar, <family>-<version>[-<variant>], is
# StackId's (stack_id.rb; its own tests are in test_stack_id.rb). -H
# used to accept only the bare version, so the name the tool had just
# printed was refused when typed back:
#
#   $ ./scripts/build_toolchain -H gcc-11.5.0 -L
#   ERROR: Unknown host GCC version: gcc-11.5.0
#   ERROR: Available: 11.5.0, 12.5.0, ...
#
# Now both work, and the two directions live next to each other so
# they cannot drift apart.
#

require_relative 'test_helper'
require_relative '../coords'

class TestStackNames < Minitest::Test

  include TestHelper

  def test_the_full_name_round_trips
    id = Coords.parse_stack("gcc-14.4.0")
    assert_equal StackId.of(Ver("14.4.0")), id
    assert_equal "gcc-14.4.0", Coords.stack_name(id)
    assert_equal "gcc-14.4.0", Coords.stack_name(Ver("14.4.0")),
                 "a version names the plain gcc stack"
  end

  # The shortcut: unambiguous, since a bare version can only mean the
  # plain gcc stack.
  def test_the_bare_version_is_accepted_too
    assert_equal Coords.parse_stack("gcc-14.4.0"),
                 Coords.parse_stack("14.4.0")
    assert_equal Ver("14.4.0"), Coords.parse_stack_ver("14.4.0")
  end

  def test_surrounding_space_is_not_a_different_stack
    assert_equal StackId.of(Ver("16.2.0")), Coords.parse_stack("  gcc-16.2.0 ")
  end

  # A variant or a foreign compiler is a stack a person can NAME; it
  # is not one the invocation can be IN, which parse_stack_ver says.
  def test_a_variant_or_foreign_stack_is_a_name_but_not_a_version
    lto = Coords.parse_stack("gcc-14.4.0-lto")
    assert_equal ["gcc", Ver("14.4.0"), "lto"], [lto.family, lto.ver,
                                                 lto.variant]
    assert_nil Coords.parse_stack_ver("gcc-14.4.0-lto")
    assert_equal "clang", Coords.parse_stack("clang-15.0.1").family
    assert_nil Coords.parse_stack_ver("clang-15.0.1")
  end

  def test_what_is_not_a_stack_is_refused
    assert_nil Coords.parse_stack("some-other")
    assert_nil Coords.parse_stack("any")
    assert_nil Coords.parse_stack("")
    assert_nil Coords.parse_stack_ver("any")
  end

  # Reading a DIRECTORY is stricter than reading what a person typed:
  # a directory is only a stack if it is spelled like one.
  def test_a_directory_must_carry_the_family
    named = Coords.new("linux-x86_64", nil, "gcc-14.4.0")
    bare  = Coords.new("linux-x86_64", nil, "14.4.0")

    assert_equal Ver("14.4.0"), named.stack_ver
    assert_equal StackId.of(Ver("14.4.0")), named.stack_id
    assert_nil bare.stack_id, "a bare version named a stack directory"
    assert_nil bare.stack_ver
  end

  def test_a_stackless_coords_has_no_version
    assert_nil Coords.new("noarch", nil, nil).stack_ver
    assert_nil Coords.new("noarch", nil, nil).stack_id
  end
end

# The product's spelling of its targets and their compilers, owned
# once each. Eight sites spelled "tilck-#{arch}" for themselves and
# one parsed it back; five spelled "gcc-#{arch}-musl". A product with
# another kernel, or an arch with a second compiler, changes the
# owner and nothing else.
class TestTargetSpelling < Minitest::Test

  include TestHelper

  RV = ALL_ARCHS["riscv64"]

  def test_the_target_machine_round_trips
    assert_equal "tilck-riscv64", Coords.target_machine(RV)
    assert Coords.target_machine?("tilck-riscv64")
    assert_equal RV, Coords.target_arch_of("tilck-riscv64")
  end

  def test_what_is_not_a_target_machine_names_no_arch
    for m in %w[linux-x86_64 noarch tilck-mips tilck- riscv64] do
      assert_nil Coords.target_arch_of(m), m
    end
    refute Coords.target_machine?("linux-x86_64")
    assert Coords.target_machine?("tilck-mips"), "a target, if unknown"
  end

  def test_target_coordinates_are_the_arch_the_board_and_the_stack
    c = Coords.target(RV, "licheerv-nano", Ver("13.3.0"))
    assert_equal "tilck-riscv64/licheerv-nano/gcc-13.3.0", c.to_s
    assert_equal RV, Coords.target_arch_of(c.machine)
  end

  def test_an_arch_names_its_cross_compiler_package
    assert_equal "gcc-riscv64-musl", RV.cross_cc_pkg
    assert_equal "gcc-i386-musl", ALL_ARCHS["i386"].cross_cc_pkg
  end
end
