# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

class TestDepResolverValidation < Minitest::Test

  def test_validate_deps_ok
    graph = { "a" => ["b"], "b" => [], "c" => ["a"] }
    DepResolver.validate_deps(graph)
  end

  def test_validate_deps_empty_graph
    DepResolver.validate_deps({})
  end

  def test_validate_deps_missing_single
    graph = { "a" => ["missing"] }
    e = assert_raises(DepResolver::MissingDepError) {
      DepResolver.validate_deps(graph)
    }
    assert_match(/a -> missing/, e.message)
  end

  def test_validate_deps_missing_multiple
    graph = { "a" => ["x"], "b" => ["y"] }
    e = assert_raises(DepResolver::MissingDepError) {
      DepResolver.validate_deps(graph)
    }
    assert_match(/a -> x/, e.message)
    assert_match(/b -> y/, e.message)
  end

  def test_no_cycles_linear
    graph = { "a" => ["b"], "b" => ["c"], "c" => [] }
    DepResolver.validate_no_cycles(graph)
  end

  def test_no_cycles_empty
    DepResolver.validate_no_cycles({})
  end

  def test_no_cycles_disconnected
    graph = { "a" => [], "b" => [], "c" => [] }
    DepResolver.validate_no_cycles(graph)
  end

  def test_cycle_direct
    graph = { "a" => ["b"], "b" => ["a"] }
    e = assert_raises(DepResolver::CycleError) {
      DepResolver.validate_no_cycles(graph)
    }
    assert_match(/a/, e.message)
    assert_match(/b/, e.message)
  end

  def test_cycle_indirect
    graph = { "a" => ["b"], "b" => ["c"], "c" => ["a"] }
    assert_raises(DepResolver::CycleError) {
      DepResolver.validate_no_cycles(graph)
    }
  end

  def test_self_cycle
    graph = { "a" => ["a"] }
    e = assert_raises(DepResolver::CycleError) {
      DepResolver.validate_no_cycles(graph)
    }
    assert_match(/a -> a/, e.message)
  end

  def test_cycle_in_subgraph_with_acyclic_nodes
    graph = {
      "ok1" => [],
      "ok2" => ["ok1"],
      "bad1" => ["bad2"],
      "bad2" => ["bad1"],
    }
    assert_raises(DepResolver::CycleError) {
      DepResolver.validate_no_cycles(graph)
    }
  end

  def test_validate_runs_both_checks
    graph = { "a" => ["missing"] }
    assert_raises(DepResolver::MissingDepError) {
      DepResolver.validate(graph)
    }
  end
end

class TestDepResolverResolve < Minitest::Test

  def test_single_no_deps
    graph = { "a" => [] }
    assert_equal ["a"], DepResolver.resolve(["a"], graph)
  end

  def test_linear_chain
    graph = { "a" => ["b"], "b" => ["c"], "c" => [] }
    assert_equal ["c", "b", "a"], DepResolver.resolve(["a"], graph)
  end

  def test_diamond
    graph = {
      "a" => ["b", "c"],
      "b" => ["d"],
      "c" => ["d"],
      "d" => []
    }
    result = DepResolver.resolve(["a"], graph)
    assert_equal 4, result.length
    assert_equal "d", result[0]
    assert_equal "a", result[-1]
    assert_equal "b", result[1]
    assert_equal "c", result[2]
  end

  def test_already_installed_leaf_skipped
    graph = { "a" => ["b"], "b" => ["c"], "c" => [] }
    result = DepResolver.resolve(["a"], graph, ["c"])
    assert_equal ["b", "a"], result
  end

  def test_already_installed_middle_skipped
    graph = { "a" => ["b"], "b" => ["c"], "c" => [] }
    result = DepResolver.resolve(["a"], graph, ["b"])
    assert_equal ["a"], result
  end

  def test_all_installed
    graph = { "a" => ["b"], "b" => [] }
    result = DepResolver.resolve(["a"], graph, ["a", "b"])
    assert_empty result
  end

  def test_requested_already_installed_but_dep_not
    graph = { "a" => ["b"], "b" => [] }
    result = DepResolver.resolve(["a"], graph, ["a"])
    assert_empty result
  end

  def test_multiple_roots
    graph = { "a" => ["c"], "b" => ["c"], "c" => [] }
    result = DepResolver.resolve(["a", "b"], graph)
    assert_operator result.index("c"), :<, result.index("a")
    assert_operator result.index("c"), :<, result.index("b")
    assert_equal 3, result.length
  end

  def test_multiple_roots_alphabetical
    graph = { "x" => [], "a" => [], "m" => [] }
    result = DepResolver.resolve(["x", "a", "m"], graph)
    assert_equal ["a", "m", "x"], result
  end

  def test_shared_transitive_dep_not_duplicated
    graph = { "a" => ["c"], "b" => ["c"], "c" => [] }
    result = DepResolver.resolve(["a", "b"], graph)
    assert_equal 1, result.count("c")
  end

  def test_missing_requested_package
    graph = { "a" => [] }
    assert_raises(DepResolver::MissingDepError) {
      DepResolver.resolve(["nonexistent"], graph)
    }
  end

  def test_deep_chain
    graph = {
      "a" => ["b"], "b" => ["c"], "c" => ["d"],
      "d" => ["e"], "e" => []
    }
    result = DepResolver.resolve(["a"], graph)
    assert_equal ["e", "d", "c", "b", "a"], result
  end

  def test_unrelated_packages_not_pulled_in
    graph = {
      "a" => ["b"], "b" => [],
      "x" => ["y"], "y" => []
    }
    result = DepResolver.resolve(["a"], graph)
    assert_equal ["b", "a"], result
    refute_includes result, "x"
    refute_includes result, "y"
  end

  def test_empty_request
    graph = { "a" => ["b"], "b" => [] }
    assert_empty DepResolver.resolve([], graph)
  end
end
