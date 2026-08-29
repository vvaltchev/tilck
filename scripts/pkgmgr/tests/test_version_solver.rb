# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

#
# VersionSolver is pure: no Package objects, no filesystem, no global
# state. These build the graph out of literals.
#

module SolverHelper

  # deps: { "name" => [PackageDep, ...] } or
  #       { "name" => { ver_string => [PackageDep, ...] } }
  def deps_of_for(deps)
    return ->(name, ver) {
      entry = deps[name]
      return [] if entry.nil?
      return entry if entry.is_a?(Array)
      return entry[ver.to_s] || entry["*"] || []
    }
  end

  def default_of_for(defaults)
    return ->(name) { defaults[name] }
  end

  def solve(roots, deps: {}, defaults: {}, notes: nil)
    return VersionSolver.resolve(
      roots,
      deps_of: deps_of_for(deps),
      default_of: default_of_for(defaults),
      on_override: notes ? ->(n, d, p, path) { notes << [n, d, p, path] } : nil,
    )
  end
end

class TestVersionSolverDefaults < Minitest::Test
  include SolverHelper

  def test_single_root_no_deps
    got = solve([["a", nil]], defaults: { "a" => Ver("1.0") })
    assert_equal({ "a" => Ver("1.0") }, got)
  end

  def test_root_with_explicit_version
    got = solve([["a", Ver("0.9")]], defaults: { "a" => Ver("1.0") })
    assert_equal({ "a" => Ver("0.9") }, got)
  end

  def test_defaults_propagate_down_a_chain
    got = solve(
      [["a", nil]],
      deps: { "a" => [Dep("b", true)], "b" => [Dep("c", true)] },
      defaults: { "a" => Ver("1.0"), "b" => Ver("2.0"), "c" => Ver("3.0") },
    )
    assert_equal({ "a" => Ver("1.0"), "b" => Ver("2.0"), "c" => Ver("3.0") },
                 got)
  end

  def test_unknown_default_is_nil_not_an_error
    got = solve([["a", nil]], deps: { "a" => [Dep("b", true)] },
                defaults: { "a" => Ver("1.0") })
    assert_nil got["b"]
  end

  def test_multiple_roots
    got = solve([["a", nil], ["b", Ver("9.9")]],
                defaults: { "a" => Ver("1.0"), "b" => Ver("2.0") })
    assert_equal({ "a" => Ver("1.0"), "b" => Ver("9.9") }, got)
  end
end

class TestVersionSolverPins < Minitest::Test
  include SolverHelper

  def test_pin_beats_default
    got = solve(
      [["a", nil]],
      deps: { "a" => [Dep("b", true, ver: Ver("1.5"))] },
      defaults: { "a" => Ver("1.0"), "b" => Ver("2.0") },
    )
    assert_equal Ver("1.5"), got["b"]
  end

  def test_pin_beats_default_reached_first_through_another_path
    # a -> x -> b (default), and a -> b pinned. Whichever edge the walk
    # sees first, the pin must win.
    got = solve(
      [["a", nil]],
      deps: {
        "a" => [Dep("x", true), Dep("b", true, ver: Ver("1.5"))],
        "x" => [Dep("b", true)],
      },
      defaults: { "a" => Ver("1.0"), "x" => Ver("1.0"), "b" => Ver("2.0") },
    )
    assert_equal Ver("1.5"), got["b"]
  end

  def test_unpinned_siblings_keep_their_defaults
    got = solve(
      [["a", nil]],
      deps: { "a" => [Dep("b", true, ver: Ver("1.5")), Dep("c", true)] },
      defaults: { "a" => Ver("1.0"), "b" => Ver("2.0"), "c" => Ver("3.0") },
    )
    assert_equal Ver("1.5"), got["b"]
    assert_equal Ver("3.0"), got["c"]
  end

  def test_two_agreeing_pins_are_fine
    got = solve(
      [["a", nil]],
      deps: {
        "a" => [Dep("x", true), Dep("y", true)],
        "x" => [Dep("b", true, ver: Ver("1.5"))],
        "y" => [Dep("b", true, ver: Ver("1.5"))],
      },
      defaults: { "a" => Ver("1.0"), "x" => Ver("1.0"),
                  "y" => Ver("1.0"), "b" => Ver("2.0") },
    )
    assert_equal Ver("1.5"), got["b"]
  end

  def test_conflicting_pins_raise_naming_both_paths
    e = assert_raises(VersionSolver::ConflictError) {
      solve(
        [["a", nil]],
        deps: {
          "a" => [Dep("x", true), Dep("y", true)],
          "x" => [Dep("b", true, ver: Ver("1.5"))],
          "y" => [Dep("b", true, ver: Ver("1.7"))],
        },
        defaults: { "a" => Ver("1.0"), "x" => Ver("1.0"),
                    "y" => Ver("1.0"), "b" => Ver("2.0") },
      )
    }
    assert_match(/\bb\b/, e.message)
    assert_match(/1\.5/, e.message)
    assert_match(/1\.7/, e.message)
    assert_match(/a -> x -> b/, e.message)
    assert_match(/a -> y -> b/, e.message)
  end

  def test_root_pin_conflicting_with_a_dep_pin_raises
    e = assert_raises(VersionSolver::ConflictError) {
      solve(
        [["b", Ver("2.5")], ["a", nil]],
        deps: { "a" => [Dep("b", true, ver: Ver("1.5"))] },
        defaults: { "a" => Ver("1.0"), "b" => Ver("2.0") },
      )
    }
    assert_match(/2\.5/, e.message)
    assert_match(/1\.5/, e.message)
  end

  def test_diamond_same_pin_twice_is_not_a_conflict
    got = solve(
      [["a", nil]],
      deps: {
        "a" => [Dep("l", true), Dep("r", true)],
        "l" => [Dep("shared", true, ver: Ver("0.5"))],
        "r" => [Dep("shared", true, ver: Ver("0.5"))],
      },
      defaults: { "a" => Ver("1.0"), "l" => Ver("1.0"),
                  "r" => Ver("1.0"), "shared" => Ver("1.0") },
    )
    assert_equal Ver("0.5"), got["shared"]
  end
end

class TestVersionSolverNotes < Minitest::Test
  include SolverHelper

  def test_override_is_reported_once_with_the_path
    notes = []
    solve(
      [["a", nil]],
      deps: { "a" => [Dep("b", true, ver: Ver("1.5"))] },
      defaults: { "a" => Ver("1.0"), "b" => Ver("2.0") },
      notes: notes,
    )
    assert_equal 1, notes.length
    name, default_ver, pinned, path = notes.first
    assert_equal "b", name
    assert_equal Ver("2.0"), default_ver
    assert_equal Ver("1.5"), pinned
    assert_equal ["a", "b"], path
  end

  def test_no_note_when_the_pin_equals_the_default
    notes = []
    solve(
      [["a", nil]],
      deps: { "a" => [Dep("b", true, ver: Ver("2.0"))] },
      defaults: { "a" => Ver("1.0"), "b" => Ver("2.0") },
      notes: notes,
    )
    assert_empty notes
  end

  def test_no_note_when_nothing_is_pinned
    notes = []
    solve([["a", nil]], deps: { "a" => [Dep("b", true)] },
          defaults: { "a" => Ver("1.0"), "b" => Ver("2.0") }, notes: notes)
    assert_empty notes
  end

  # The solver iterates to a fixed point; a package whose default was
  # displaced must be reported once, not once per pass.
  def test_note_not_repeated_per_pass
    notes = []
    solve(
      [["a", nil]],
      deps: {
        "a" => { "*" => [Dep("b", true, ver: Ver("1.5"))] },
        "b" => { "1.5" => [Dep("c", true, ver: Ver("0.1"))], "*" => [] },
      },
      defaults: { "a" => Ver("1.0"), "b" => Ver("2.0"), "c" => Ver("9.0") },
      notes: notes,
    )
    assert_equal 2, notes.length
    assert_equal ["b", "c"], notes.map(&:first).sort
  end
end

class TestVersionSolverVersionDependentDeps < Minitest::Test
  include SolverHelper

  # The dependency list may itself depend on the version chosen, so
  # binding one version can pull in another. One pass is not enough.
  def test_dep_list_that_depends_on_the_bound_version
    got = solve(
      [["a", nil]],
      deps: {
        "a" => { "*" => [Dep("b", true, ver: Ver("1.5"))] },
        # b only needs `extra` at 1.5, not at its default 2.0.
        "b" => { "1.5" => [Dep("extra", true)], "2.0" => [] },
      },
      defaults: { "a" => Ver("1.0"), "b" => Ver("2.0"),
                  "extra" => Ver("7.0") },
    )
    assert_equal Ver("1.5"), got["b"]
    assert_equal Ver("7.0"), got["extra"]
  end

  def test_settles_on_a_stable_graph
    got = solve(
      [["a", nil]],
      deps: { "a" => [Dep("b", true)], "b" => [Dep("a", true)] },
      defaults: { "a" => Ver("1.0"), "b" => Ver("2.0") },
    )
    assert_equal({ "a" => Ver("1.0"), "b" => Ver("2.0") }, got)
  end

  # A deps_of that never stops changing must be reported, not spun on.
  def test_unstable_graph_raises
    n = 0
    assert_raises(VersionSolver::UnstableError) {
      VersionSolver.resolve(
        [["a", nil]],
        deps_of: ->(name, ver) {
          next [] if name != "a"
          n += 1
          [Dep("b", true, ver: Ver("#{n}.0"))]
        },
        default_of: ->(name) { Ver("1.0") },
      )
    }
  end
end
