# SPDX-License-Identifier: BSD-2-Clause
#
# How a stack is spelled, read in both directions.
#
# A stack is "gcc-14.4.0" -- in the path, in the -L listing, in every
# message. -H used to accept only the bare version, so the name the
# tool had just printed was refused when typed back:
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
    assert_equal Ver("14.4.0"), Coords.parse_stack("gcc-14.4.0")
    assert_equal "gcc-14.4.0", Coords.stack_name(Ver("14.4.0"))
  end

  # The shortcut: unambiguous while there is one kind of stack.
  def test_the_bare_version_is_accepted_too
    assert_equal Coords.parse_stack("gcc-14.4.0"),
                 Coords.parse_stack("14.4.0")
  end

  def test_surrounding_space_is_not_a_different_stack
    assert_equal Ver("16.2.0"), Coords.parse_stack("  gcc-16.2.0 ")
  end

  def test_what_is_not_a_stack_is_refused
    assert_nil Coords.parse_stack("clang-15")
    assert_nil Coords.parse_stack("any")
    assert_nil Coords.parse_stack("")
  end

  # Reading a DIRECTORY is stricter than reading what a person typed:
  # a directory is only a stack if it is spelled like one.
  def test_a_directory_must_carry_the_prefix
    named = Coords.new("linux-x86_64", nil, "gcc-14.4.0")
    bare  = Coords.new("linux-x86_64", nil, "14.4.0")

    assert_equal Ver("14.4.0"), named.stack_ver
    assert_nil bare.stack_ver, "a bare version named a stack directory"
  end

  def test_a_stackless_coords_has_no_version
    assert_nil Coords.new("noarch", nil, nil).stack_ver
  end
end
