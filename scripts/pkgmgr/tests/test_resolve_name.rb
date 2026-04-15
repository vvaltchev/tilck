# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

class TestResolveNameExactMatch < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_exact_match_wins_over_substring
    # "lua" is exactly a package AND a substring of "micropython-lua".
    # Exact match must win — no ambiguity error.
    pkgmgr.register(FakePackage.new("lua"))
    pkgmgr.register(FakePackage.new("micropython-lua"))
    name, matches = pkgmgr.resolve_name("lua")
    assert_equal "lua", name
    assert_nil matches
  end

  def test_exact_match_unique_package
    pkgmgr.register(FakePackage.new("foo"))
    name, matches = pkgmgr.resolve_name("foo")
    assert_equal "foo", name
    assert_nil matches
  end
end

class TestResolveNameUniqueSubstring < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_substring_at_end_unique
    # "mtools" uniquely matches "host_mtools" (ends with).
    pkgmgr.register(FakePackage.new("host_mtools", on_host: true))
    pkgmgr.register(FakePackage.new("other"))
    name, matches = pkgmgr.resolve_name("mtools")
    assert_equal "host_mtools", name
    assert_nil matches
  end

  def test_substring_at_start_unique
    # "micro" uniquely matches "micropython" (starts with).
    pkgmgr.register(FakePackage.new("micropython"))
    pkgmgr.register(FakePackage.new("other"))
    name, matches = pkgmgr.resolve_name("micro")
    assert_equal "micropython", name
    assert_nil matches
  end

  def test_substring_in_middle_unique
    # "copy" uniquely matches "some-copy-tool" (middle).
    pkgmgr.register(FakePackage.new("some-copy-tool"))
    pkgmgr.register(FakePackage.new("other"))
    name, matches = pkgmgr.resolve_name("copy")
    assert_equal "some-copy-tool", name
    assert_nil matches
  end

  def test_substring_is_full_name_of_other_pkg
    # "python" uniquely matches "micropython" when no exact "python"
    # package is registered. The substring is the full name of what
    # could be another package — but since it isn't registered,
    # micropython wins.
    pkgmgr.register(FakePackage.new("micropython"))
    name, matches = pkgmgr.resolve_name("python")
    assert_equal "micropython", name
    assert_nil matches
  end
end

class TestResolveNameNoMatch < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_no_match_empty_registry
    name, matches = pkgmgr.resolve_name("anything")
    assert_nil name
    assert_equal [], matches
  end

  def test_no_match_with_packages
    pkgmgr.register(FakePackage.new("foo"))
    pkgmgr.register(FakePackage.new("bar"))
    name, matches = pkgmgr.resolve_name("zzz")
    assert_nil name
    assert_equal [], matches
  end
end

class TestResolveNameAmbiguous < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_two_matches_both_substring
    # "tools" matches "host_tools" (ends) and "tools_of_the_trade"
    # (starts). Ordering: starts_with first, then ends_with.
    pkgmgr.register(FakePackage.new("host_tools", on_host: true))
    pkgmgr.register(FakePackage.new("tools_of_the_trade"))
    name, matches = pkgmgr.resolve_name("tools")
    assert_nil name
    assert_equal 2, matches.length
    assert_equal "tools_of_the_trade", matches[0]  # starts_with
    assert_equal "host_tools",         matches[1]  # ends_with
  end

  def test_multiple_matches_ordering_starts_ends_middle
    # "mid" matches:
    #   "mid_start"   — starts_with
    #   "mid_here"    — starts_with
    #   "end_mid"     — ends_with
    #   "x_mid_y"     — middle
    # Expected order: two starts_with (in @packages insertion order),
    # then ends_with, then middle.
    pkgmgr.register(FakePackage.new("mid_start"))
    pkgmgr.register(FakePackage.new("mid_here"))
    pkgmgr.register(FakePackage.new("end_mid"))
    pkgmgr.register(FakePackage.new("x_mid_y"))
    name, matches = pkgmgr.resolve_name("mid")
    assert_nil name
    # Check group partitioning. Within a group, order is determined
    # by @packages hash insertion order — we assert group membership
    # by position ranges, not exact positions.
    assert_equal 4, matches.length
    starts_group = matches[0..1]
    ends_group   = matches[2..2]
    middle_group = matches[3..3]
    assert_includes starts_group, "mid_start"
    assert_includes starts_group, "mid_here"
    assert_equal ["end_mid"], ends_group
    assert_equal ["x_mid_y"], middle_group
  end

  def test_ambiguous_three_matches
    pkgmgr.register(FakePackage.new("aaa_foo"))
    pkgmgr.register(FakePackage.new("bbb_foo"))
    pkgmgr.register(FakePackage.new("ccc_foo"))
    name, matches = pkgmgr.resolve_name("foo")
    assert_nil name
    assert_equal 3, matches.length
  end

  def test_ambiguous_more_than_three_matches
    pkgmgr.register(FakePackage.new("aaa_foo"))
    pkgmgr.register(FakePackage.new("bbb_foo"))
    pkgmgr.register(FakePackage.new("ccc_foo"))
    pkgmgr.register(FakePackage.new("ddd_foo"))
    pkgmgr.register(FakePackage.new("eee_foo"))
    name, matches = pkgmgr.resolve_name("foo")
    assert_nil name
    assert_equal 5, matches.length  # resolve returns ALL, caller trims
  end

  def test_ambiguous_mixed_positions
    # "a" matches everything. Ordering groups:
    #   starts_with: apple, ant        (start with "a")
    #   ends_with:   pizza, cobra      (end with "a", don't start with it)
    #   middle:      watch, banal      (contain "a" but not at start or end)
    pkgmgr.register(FakePackage.new("apple"))
    pkgmgr.register(FakePackage.new("pizza"))
    pkgmgr.register(FakePackage.new("watch"))
    pkgmgr.register(FakePackage.new("ant"))
    pkgmgr.register(FakePackage.new("cobra"))
    pkgmgr.register(FakePackage.new("banal"))
    name, matches = pkgmgr.resolve_name("a")
    assert_nil name
    assert_equal 6, matches.length
    # Verify group partitioning by slot.
    starts = matches[0..1]
    ends   = matches[2..3]
    middle = matches[4..5]
    assert_equal ["apple", "ant"].sort,   starts.sort
    assert_equal ["pizza", "cobra"].sort, ends.sort
    assert_equal ["watch", "banal"].sort, middle.sort
  end

  def test_ambiguous_starts_and_middle_only
    # "lib" matches:
    #   libmusl    — starts_with
    #   libyaml    — starts_with
    #   some_lib_x — middle
    pkgmgr.register(FakePackage.new("libmusl"))
    pkgmgr.register(FakePackage.new("libyaml"))
    pkgmgr.register(FakePackage.new("some_lib_x"))
    name, matches = pkgmgr.resolve_name("lib")
    assert_nil name
    assert_equal 3, matches.length
    assert_includes matches[0..1], "libmusl"
    assert_includes matches[0..1], "libyaml"
    assert_equal "some_lib_x", matches[2]
  end

  def test_ambiguous_ends_and_middle_only
    # "tools" matches:
    #   host_tools  — ends_with
    #   my_tools    — ends_with
    #   no starts_with for this one
    pkgmgr.register(FakePackage.new("host_tools", on_host: true))
    pkgmgr.register(FakePackage.new("my_tools"))
    name, matches = pkgmgr.resolve_name("tools")
    assert_nil name
    assert_equal 2, matches.length
    # Both are ends_with, so either order is fine within the group.
    assert_equal ["host_tools", "my_tools"].sort, matches.sort
  end
end

class TestResolveNameEdgeCases < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_empty_input_matches_all
    # Empty substring is contained in every name, so this is ambiguous
    # unless there's exactly one package.
    pkgmgr.register(FakePackage.new("foo"))
    pkgmgr.register(FakePackage.new("bar"))
    name, matches = pkgmgr.resolve_name("")
    assert_nil name
    # Both names start_with("") so both go in the starts group.
    assert_equal 2, matches.length
  end

  def test_empty_input_with_single_package
    # Exactly one package — empty substring resolves uniquely to it.
    pkgmgr.register(FakePackage.new("foo"))
    name, matches = pkgmgr.resolve_name("")
    assert_equal "foo", name
    assert_nil matches
  end

  def test_input_is_entire_name_but_not_exact
    # Input "lua" matches package "lua" exactly AND is a substring of
    # "lua_ext". Exact match must win.
    pkgmgr.register(FakePackage.new("lua_ext"))
    pkgmgr.register(FakePackage.new("lua"))
    name, matches = pkgmgr.resolve_name("lua")
    assert_equal "lua", name
    assert_nil matches
  end

  def test_case_sensitive_no_match
    # Resolution is case-sensitive.
    pkgmgr.register(FakePackage.new("MyPkg"))
    name, matches = pkgmgr.resolve_name("mypkg")
    assert_nil name
    assert_equal [], matches
  end
end
