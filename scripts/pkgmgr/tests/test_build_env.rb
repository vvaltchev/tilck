# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

#
# BuildEnv is pure: no filesystem, no Package objects, no global state.
#

class TestBuildEnvConstruction < Minitest::Test

  def test_empty_has_nothing
    be = BuildEnv.empty
    assert be.empty?
    assert_equal [], be.include_dirs
    assert_equal [], be.lib_dirs
    assert_equal [], be.pkg_config_dirs
    assert_equal({}, be.extra_env)
  end

  def test_new_with_no_args_is_empty
    assert BuildEnv.new.empty?
  end

  def test_not_empty_with_include_dirs
    assert !BuildEnv.new(include_dirs: ["/a"]).empty?
  end

  def test_not_empty_with_lib_dirs
    assert !BuildEnv.new(lib_dirs: ["/a"]).empty?
  end

  def test_not_empty_with_pkg_config_dirs
    assert !BuildEnv.new(pkg_config_dirs: ["/a"]).empty?
  end

  def test_not_empty_with_extra_env
    assert !BuildEnv.new(extra_env: { "K" => "v" }).empty?
  end

  def test_pathnames_are_stringified
    be = BuildEnv.new(include_dirs: [Pathname.new("/a/inc")])
    assert_equal ["/a/inc"], be.include_dirs
    assert_kind_of String, be.include_dirs.first
  end

  def test_single_value_accepted_without_array
    be = BuildEnv.new(include_dirs: Pathname.new("/a"), lib_dirs: "/b")
    assert_equal ["/a"], be.include_dirs
    assert_equal ["/b"], be.lib_dirs
  end

  def test_nil_becomes_empty_list
    be = BuildEnv.new(include_dirs: nil)
    assert_equal [], be.include_dirs
  end

  def test_duplicates_removed_keeping_first_occurrence
    be = BuildEnv.new(include_dirs: ["/a", "/b", "/a", "/c", "/b"])
    assert_equal ["/a", "/b", "/c"], be.include_dirs
  end

  def test_pathname_and_string_of_same_dir_dedup
    be = BuildEnv.new(include_dirs: [Pathname.new("/a"), "/a"])
    assert_equal ["/a"], be.include_dirs
  end

  def test_extra_env_keys_and_values_stringified
    be = BuildEnv.new(extra_env: { :SOME_VAR => Pathname.new("/p") })
    assert_equal({ "SOME_VAR" => "/p" }, be.extra_env)
  end

  def test_instance_and_collections_are_frozen
    be = BuildEnv.new(include_dirs: ["/a"], extra_env: { "K" => "v" })
    assert be.frozen?
    assert be.include_dirs.frozen?
    assert be.lib_dirs.frozen?
    assert be.pkg_config_dirs.frozen?
    assert be.extra_env.frozen?
    assert_raises(FrozenError) { be.include_dirs << "/evil" }
  end

  def test_normalize_directly
    assert_equal [], BuildEnv.normalize(nil)
    assert_equal ["/a"], BuildEnv.normalize("/a")
    assert_equal ["/a", "/b"], BuildEnv.normalize(["/a", "/b", "/a"])
  end
end

class TestBuildEnvMerge < Minitest::Test

  def test_merge_with_nil_returns_self
    be = BuildEnv.new(include_dirs: ["/a"])
    assert_same be, be.merge(nil)
  end

  def test_merge_with_empty_returns_self
    be = BuildEnv.new(include_dirs: ["/a"])
    assert_same be, be.merge(BuildEnv.empty)
  end

  def test_empty_merge_other_returns_other
    other = BuildEnv.new(include_dirs: ["/a"])
    assert_same other, BuildEnv.empty.merge(other)
  end

  def test_empty_merge_empty_is_empty
    assert BuildEnv.empty.merge(BuildEnv.empty).empty?
  end

  def test_merge_preserves_order_self_first
    a = BuildEnv.new(include_dirs: ["/a1", "/a2"])
    b = BuildEnv.new(include_dirs: ["/b1"])
    assert_equal ["/a1", "/a2", "/b1"], a.merge(b).include_dirs
    assert_equal ["/b1", "/a1", "/a2"], b.merge(a).include_dirs
  end

  def test_merge_dedups_shared_dir_keeping_first_position
    a = BuildEnv.new(include_dirs: ["/common", "/a"])
    b = BuildEnv.new(include_dirs: ["/b", "/common"])
    assert_equal ["/common", "/a", "/b"], a.merge(b).include_dirs
  end

  def test_merge_combines_every_field
    a = BuildEnv.new(include_dirs: ["/ai"], lib_dirs: ["/al"],
                     pkg_config_dirs: ["/ap"], extra_env: { "A" => "1" })
    b = BuildEnv.new(include_dirs: ["/bi"], lib_dirs: ["/bl"],
                     pkg_config_dirs: ["/bp"], extra_env: { "B" => "2" })
    m = a.merge(b)
    assert_equal ["/ai", "/bi"], m.include_dirs
    assert_equal ["/al", "/bl"], m.lib_dirs
    assert_equal ["/ap", "/bp"], m.pkg_config_dirs
    assert_equal({ "A" => "1", "B" => "2" }, m.extra_env)
  end

  def test_merge_does_not_mutate_operands
    a = BuildEnv.new(include_dirs: ["/a"])
    b = BuildEnv.new(include_dirs: ["/b"])
    a.merge(b)
    assert_equal ["/a"], a.include_dirs
    assert_equal ["/b"], b.include_dirs
  end

  def test_merge_same_env_key_same_value_is_fine
    a = BuildEnv.new(extra_env: { "K" => "v" })
    b = BuildEnv.new(extra_env: { "K" => "v" })
    assert_equal({ "K" => "v" }, a.merge(b).extra_env)
  end

  def test_merge_conflicting_env_value_raises
    a = BuildEnv.new(extra_env: { "K" => "one" })
    b = BuildEnv.new(extra_env: { "K" => "two" })
    e = assert_raises(BuildEnv::ConflictError) { a.merge(b) }
    assert_match(/K/, e.message)
    assert_match(/one/, e.message)
    assert_match(/two/, e.message)
  end

  def test_merge_is_associative_over_three_providers
    a = BuildEnv.new(include_dirs: ["/a"])
    b = BuildEnv.new(include_dirs: ["/b"])
    c = BuildEnv.new(include_dirs: ["/c"])
    assert_equal a.merge(b).merge(c).include_dirs,
                 a.merge(b.merge(c)).include_dirs
  end

  def test_reduce_over_many_providers
    envs = (1..5).map { |i| BuildEnv.new(include_dirs: ["/inc#{i}"]) }
    m = envs.reduce(BuildEnv.empty) { |acc, e| acc.merge(e) }
    assert_equal ["/inc1", "/inc2", "/inc3", "/inc4", "/inc5"], m.include_dirs
  end
end

class TestBuildEnvRendering < Minitest::Test

  def test_cflags_empty
    assert_equal "", BuildEnv.empty.cflags
  end

  def test_cflags_single_and_multiple
    assert_equal "-I/a", BuildEnv.new(include_dirs: ["/a"]).cflags
    assert_equal "-I/a -I/b",
                 BuildEnv.new(include_dirs: ["/a", "/b"]).cflags
  end

  def test_ldflags_empty_single_multiple
    assert_equal "", BuildEnv.empty.ldflags
    assert_equal "-L/a", BuildEnv.new(lib_dirs: ["/a"]).ldflags
    assert_equal "-L/a -L/b", BuildEnv.new(lib_dirs: ["/a", "/b"]).ldflags
  end

  def test_env_without_pkg_config_dirs_is_just_extra_env
    be = BuildEnv.new(extra_env: { "K" => "v" })
    assert_equal({ "K" => "v" }, be.env({}))
  end

  def test_env_prepends_pkg_config_path_to_inherited
    be = BuildEnv.new(pkg_config_dirs: ["/mine"])
    got = be.env({ "PKG_CONFIG_PATH" => "/system" })
    assert_equal "/mine:/system", got["PKG_CONFIG_PATH"]
  end

  def test_env_without_inherited_has_no_trailing_colon
    be = BuildEnv.new(pkg_config_dirs: ["/mine"])
    assert_equal "/mine", be.env({})["PKG_CONFIG_PATH"]
    assert_equal "/mine", be.env({ "PKG_CONFIG_PATH" => "" })["PKG_CONFIG_PATH"]
  end

  def test_env_joins_multiple_pkg_config_dirs
    be = BuildEnv.new(pkg_config_dirs: ["/a", "/b"])
    assert_equal "/a:/b", be.env({})["PKG_CONFIG_PATH"]
  end

  def test_env_keeps_extra_env_alongside_pkg_config_path
    be = BuildEnv.new(pkg_config_dirs: ["/p"], extra_env: { "K" => "v" })
    got = be.env({})
    assert_equal "v", got["K"]
    assert_equal "/p", got["PKG_CONFIG_PATH"]
  end

  def test_env_result_is_a_fresh_mutable_hash
    be = BuildEnv.new(extra_env: { "K" => "v" })
    got = be.env({})
    got["OTHER"] = "x"
    assert_equal({ "K" => "v" }, be.extra_env)
  end

  def test_kconfig_make_vars_empty
    assert_equal [], BuildEnv.empty.kconfig_make_vars
  end

  def test_kconfig_make_vars_only_includes
    be = BuildEnv.new(include_dirs: ["/a"])
    assert_equal ["HOSTCFLAGS=-I/a"], be.kconfig_make_vars
  end

  def test_kconfig_make_vars_only_libs
    be = BuildEnv.new(lib_dirs: ["/a"])
    assert_equal ["HOSTLDFLAGS=-L/a"], be.kconfig_make_vars
  end

  def test_kconfig_make_vars_both_in_order
    be = BuildEnv.new(include_dirs: ["/i"], lib_dirs: ["/l"])
    assert_equal ["HOSTCFLAGS=-I/i", "HOSTLDFLAGS=-L/l"], be.kconfig_make_vars
  end

  #
  # The regression this whole value object exists for.
  #
  # GNU make keeps the LAST assignment of a variable given on the
  # command line, so two providers each contributing a ready-made
  # "HOSTCFLAGS=..." string would silently drop the first one's
  # include paths. Verified against GNU Make 4.3:
  #
  #   $ make HOSTCFLAGS="-I/nc/include" HOSTCFLAGS="-I/z/include"
  #   HOSTCFLAGS=[-I/z/include]
  #
  # Merging must therefore produce exactly ONE assignment per variable,
  # carrying every provider's paths.
  #
  def test_two_providers_yield_one_assignment_per_variable
    ncurses = BuildEnv.new(
      include_dirs: ["/nc/include", "/nc/include/ncursesw"],
      lib_dirs: ["/nc/lib"],
    )
    zlib = BuildEnv.new(include_dirs: ["/z/include"], lib_dirs: ["/z/lib"])

    vars = ncurses.merge(zlib).kconfig_make_vars

    assert_equal 1, vars.count { |v| v.start_with?("HOSTCFLAGS=") }
    assert_equal 1, vars.count { |v| v.start_with?("HOSTLDFLAGS=") }
    assert_equal ["HOSTCFLAGS=-I/nc/include -I/nc/include/ncursesw -I/z/include",
                  "HOSTLDFLAGS=-L/nc/lib -L/z/lib"], vars
  end

  def test_two_providers_pkg_config_path_keeps_both
    a = BuildEnv.new(pkg_config_dirs: ["/a/lib/pkgconfig"])
    b = BuildEnv.new(pkg_config_dirs: ["/b/lib/pkgconfig"])
    got = a.merge(b).env({})
    assert_equal "/a/lib/pkgconfig:/b/lib/pkgconfig", got["PKG_CONFIG_PATH"]
  end
end

#
# bin_dirs: a dependency that ships a tool the build INVOKES.
#
# meson looks for ninja on PATH and cannot be told about it any other
# way, so publishing the directory has to be a property of the
# dependency rather than something each consumer arranges for itself.
#
class TestBuildEnvBinDirs < Minitest::Test

  def test_bin_dirs_reach_path
    be = BuildEnv.new(bin_dirs: ["/tools/bin"])
    assert_equal "/tools/bin:/usr/bin", be.env({ "PATH" => "/usr/bin" })["PATH"]
  end

  def test_bin_dirs_come_first
    be = BuildEnv.new(bin_dirs: ["/ours"])
    assert be.env({ "PATH" => "/usr/bin" })["PATH"].start_with?("/ours:")
  end

  def test_no_bin_dirs_leaves_path_alone
    refute BuildEnv.new(include_dirs: ["/a"]).env({ "PATH" => "/usr/bin" })
                   .key?("PATH")
  end

  def test_bin_dirs_without_an_inherited_path
    be = BuildEnv.new(bin_dirs: ["/ours"])
    assert_equal "/ours", be.env({})["PATH"]
  end

  def test_two_providers_contribute_both_directories
    a = BuildEnv.new(bin_dirs: ["/a/bin"])
    b = BuildEnv.new(bin_dirs: ["/b/bin"])
    assert_equal "/a/bin:/b/bin:/usr/bin",
                 a.merge(b).env({ "PATH" => "/usr/bin" })["PATH"]
  end

  def test_bin_dirs_count_towards_emptiness
    refute BuildEnv.new(bin_dirs: ["/a"]).empty?
    assert BuildEnv.empty.bin_dirs.empty?
  end

  def test_bin_dirs_dedup_like_the_others
    be = BuildEnv.new(bin_dirs: ["/a", "/b", "/a"])
    assert_equal ["/a", "/b"], be.bin_dirs
  end
end
