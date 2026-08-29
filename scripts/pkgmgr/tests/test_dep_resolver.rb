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

#
# dep_closure: nearest-first (breadth-first) transitive closure, used
# for collecting the build flags a package's dependencies publish.
# Deliberately a different order than resolve()'s topological sort.
#
class TestDepResolverClosure < Minitest::Test

  def test_closure_excludes_self
    graph = { "a" => ["b"], "b" => [] }
    assert_equal ["b"], DepResolver.dep_closure("a", graph)
  end

  def test_closure_no_deps
    graph = { "a" => [] }
    assert_equal [], DepResolver.dep_closure("a", graph)
  end

  def test_closure_linear_chain
    graph = { "a" => ["b"], "b" => ["c"], "c" => [] }
    assert_equal ["b", "c"], DepResolver.dep_closure("a", graph)
  end

  def test_closure_is_nearest_first_not_reversed_toposort
    # a depends on b and c; c depends on d. Breadth-first puts both
    # direct deps (b, c) ahead of the transitive one (d). Reversing
    # resolve()'s topological order would not: it can place d before b.
    graph = { "a" => ["b", "c"], "b" => [], "c" => ["d"], "d" => [] }
    assert_equal ["b", "c", "d"], DepResolver.dep_closure("a", graph)

    topo = DepResolver.resolve(["a"], graph)
    assert_equal ["b", "d", "c", "a"], topo
    assert_equal ["c", "d", "b"], topo.reverse.reject { |n| n == "a" }
  end

  def test_closure_diamond_reports_shared_dep_once
    graph = { "a" => ["b", "c"], "b" => ["d"], "c" => ["d"], "d" => [] }
    got = DepResolver.dep_closure("a", graph)
    assert_equal ["b", "c", "d"], got
    assert_equal 1, got.count("d")
  end

  def test_closure_deep_tree_level_order
    graph = {
      "root" => ["l1a", "l1b"],
      "l1a"  => ["l2a"],
      "l1b"  => ["l2b"],
      "l2a"  => [],
      "l2b"  => [],
    }
    assert_equal ["l1a", "l1b", "l2a", "l2b"],
                 DepResolver.dep_closure("root", graph)
  end

  def test_closure_unknown_package_raises
    e = assert_raises(DepResolver::MissingDepError) {
      DepResolver.dep_closure("nope", { "a" => [] })
    }
    assert_match(/nope/, e.message)
  end

  def test_closure_terminates_on_cycle
    # validate_no_cycles rejects these before install, but dep_closure
    # must not hang if it is ever handed one.
    graph = { "a" => ["b"], "b" => ["c"], "c" => ["a"] }
    assert_equal ["b", "c"], DepResolver.dep_closure("a", graph)
  end

  def test_closure_self_loop_terminates
    graph = { "a" => ["a", "b"], "b" => [] }
    assert_equal ["b"], DepResolver.dep_closure("a", graph)
  end

  def test_closure_does_not_mutate_graph
    graph = { "a" => ["b"], "b" => [] }
    DepResolver.dep_closure("a", graph)
    assert_equal({ "a" => ["b"], "b" => [] }, graph)
  end
end
