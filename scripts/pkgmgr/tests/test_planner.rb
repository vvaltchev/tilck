# SPDX-License-Identifier: BSD-2-Clause
#
# THE PLANNER, ON VALUES.
#
# Every case hands the planner a World built in memory -- the installs
# a scan found, with no tree behind them any more -- and reads the
# Plan back. Nothing here runs a build: what -s would do is a value,
# and these pin what the value says for the shapes the bugs had.
#

require_relative 'test_helper'

class TestPlanner < Minitest::Test

  include TestHelper

  HOST = { on_host: true, host_tier: :distro,
           arch_list: ALL_HOST_ARCHS.values }.freeze
  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]

  def setup
    reset_pkgmgr!
  end

  # a -> b -> c, registered.
  def chain
    c = FakePackage.new("c")
    b = FakePackage.new("b", dep_list: [Dep("c", false)])
    a = FakePackage.new("a", dep_list: [Dep("b", false)])
    [a, b, c].each { |p| pkgmgr.register(p) }
    return [a, b, c]
  end

  # The world as a value, cut loose from the tree it was read from.
  def world_now = World.of(World.scan(pkgmgr.all_packages).installs)

  def plan(requested, **kw)
    return Planner.plan_install(pkgmgr, world_now, requested, pkgmgr.scope,
                                **kw)
  end

  # --- what is built, in what order, with what marks -----------------------

  def test_the_closure_is_built_dependencies_first_and_deps_are_auto
    with_fake_tc do
      chain
      p = plan([["a", nil]])
      assert_equal %w[c b a], p.builds.map(&:name)
      assert_equal %i[auto auto manual], p.builds.map(&:mark)
      assert_equal %i[default default default], p.builds.map(&:origin)
      assert_empty p.removes
      assert_empty p.marks
    end
  end

  def test_what_is_installed_at_its_bound_version_is_not_built
    with_fake_tc do
      a, b, c = chain
      fake_install(c)
      p = plan([["a", nil]])
      assert_equal %w[b a], p.builds.map(&:name)
      fake_install(b)
      fake_install(a)
      assert_empty plan([["a", nil]]).builds
    end
  end

  # A version asked for by name is pinned even when it is the default;
  # a dependency's is pinned only when a pin moved it off the default.
  def test_origin_is_pinned_for_a_named_version_and_a_moved_one
    with_fake_tc do
      gmp = FakePackage.new("host_gmp", **HOST)
      gmp.define_singleton_method(:default_ver) { Ver("2.0.0") }
      gmp.define_singleton_method(:installable_versions) {
        [Ver("1.0.0"), Ver("2.0.0")]
      }
      pinner = FakePackage.new("host_pinner", **HOST,
                               dep_list: [Dep("host_gmp", true,
                                              ver: Ver("1.0.0"))])
      [gmp, pinner].each { |p| pkgmgr.register(p) }

      p = plan([["host_pinner", Ver("1.0.0")]])
      by = p.builds.to_h { |b| [b.name, b] }
      assert_equal :pinned, by["host_pinner"].origin, "named"
      assert_equal :pinned, by["host_gmp"].origin, "moved by the pin"
      assert_equal Ver("1.0.0"), by["host_gmp"].ver
      assert_equal({ "host_gmp" => Ver("1.0.0") }, by["host_pinner"].against)
      assert_equal Ver("1.0.0"), by["host_pinner"].bound["host_gmp"]
      assert_match(/host_gmp: using 1.0.0, not the default 2.0.0/,
                   p.notes.join("\n"))
    end
  end

  def test_two_pins_that_disagree_are_a_refusal_not_a_plan
    with_fake_tc do
      x = FakePackage.new("host_x", **HOST)
      x.define_singleton_method(:installable_versions) {
        [Ver("1.0.0"), Ver("2.0.0")]
      }
      a = FakePackage.new("host_a", **HOST,
                          dep_list: [Dep("host_x", true, ver: Ver("1.0.0"))])
      b = FakePackage.new("host_b", **HOST,
                          dep_list: [Dep("host_x", true, ver: Ver("2.0.0"))])
      [x, a, b].each { |p| pkgmgr.register(p) }

      r = plan([["host_a", nil], ["host_b", nil]])
      assert_kind_of Refusal, r
      assert_match(/Version conflict/, r.message)
    end
  end

  # --- -f: what goes is what comes back ------------------------------------

  def test_force_removes_exactly_what_the_request_recreates
    with_fake_tc do
      a, b, c = chain
      [a, b, c].each { |p| fake_install(p) }
      p = plan([["a", nil]], force: true)
      assert_equal ["a"], p.removes.map { |r| r.install.pkgname }
      assert_equal ["a"], p.builds.map(&:name), "the dependencies stay"
      assert_equal p.actions.first, p.removes.first, "removal comes first"
    end
  end

  def test_force_on_what_is_not_installed_is_an_install
    with_fake_tc do
      chain
      p = plan([["a", nil]], force: true)
      assert_empty p.removes
      assert_equal %w[c b a], p.builds.map(&:name)
    end
  end

  # --- claims ----------------------------------------------------------------

  def test_a_claimed_package_already_here_as_a_dependency_is_re_marked
    with_fake_tc do
      _, b, c = chain
      fake_install(c, mark: :auto)
      fake_install(b, mark: :auto)
      p = plan([["b", nil]])
      assert_equal ["b"], p.marks.map { |m| m.install.pkgname }
      assert p.marks.first.manual
      assert_empty p.builds
      assert_empty plan([["b", nil]], claimed: []).marks, "not claimed"
    end
  end

  def test_an_unclaimed_root_inherits_the_mark_of_what_it_replaces
    with_fake_tc do
      t = FakePackage.new("t")
      t.define_singleton_method(:installable_versions) {
        [Ver("1.0.0"), Ver("2.0.0")]
      }
      pkgmgr.register(t)
      fake_install(t, Ver("1.0.0"), mark: :manual)
      b = plan([["t", Ver("2.0.0")]], claimed: []).builds.first
      assert_equal :manual, b.mark, "the old default install was the user's"
      p2 = plan([["t", Ver("2.0.0")]], claimed: ["t"]).builds.first
      assert_equal :manual, p2.mark
    end
  end

  # --- the stack -----------------------------------------------------------

  # A request that binds host_gcc builds into that stack; one that
  # binds no compiler builds into the stack in effect.
  def test_the_plan_builds_into_the_stack_the_request_resolves_to
    with_fake_tc do
      gcc = FakePackage.new("host_gcc", **HOST)
      gcc.define_singleton_method(:installable_versions) {
        [Ver("7.7.7"), Ver("8.8.8")]
      }
      gcc.define_singleton_method(:default_ver) { scope.stack }
      s = FakePackage.new("host_s", on_host: true, host_tier: :stack,
                          arch_list: ALL_HOST_ARCHS.values,
                          dep_list: [Dep("host_gcc", true)])
      [gcc, s].each { |p| pkgmgr.register(p) }
      pkgmgr.host_stack = Ver("7.7.7")

      p = plan([["host_gcc", Ver("8.8.8")]])
      assert_equal Ver("8.8.8"), p.scope.stack
      assert_equal Ver("7.7.7"), plan([["host_s", nil]]).scope.stack
      assert_equal Ver("7.7.7"), plan([["host_s", nil]]).bound["host_gcc"]
    end
  end

  # A target package is built with the cross compiler of its arch, a
  # dependency it does not declare. The binder has to see it as the
  # graph does, or the compiler is in the plan with no version bound.
  def test_the_implicit_cross_compiler_is_bound_and_built_first
    with_fake_tc do
      t = FakePackage.new("t")
      cc = FakePackage.new("gcc-#{ARCH.name}-musl", on_host: true,
                           is_compiler: true, host_tier: :portable,
                           arch_list: ALL_HOST_ARCHS.values,
                           target_arch: ARCH)
      [t, cc].each { |p| pkgmgr.register(p) }

      p = plan([["t", nil]])
      assert_equal [cc.name, "t"], p.builds.map(&:name)
      assert_equal cc.default_ver, p.bound[cc.name]
      refute_nil p.builds.first.ver

      fake_install(cc)
      assert_equal ["t"], plan([["t", nil]]).builds.map(&:name),
                   "an installed compiler is not in the plan"
    end
  end

  # A target that declares its compiler itself gets it once, not twice.
  def test_a_declared_cross_compiler_dependency_is_not_added_again
    with_fake_tc do
      cc_name = "gcc-#{ARCH.name}-musl"
      t = FakePackage.new("t", dep_list: [Dep(cc_name, true)])
      cc = FakePackage.new(cc_name, on_host: true, is_compiler: true,
                           host_tier: :portable,
                           arch_list: ALL_HOST_ARCHS.values,
                           target_arch: ARCH)
      [t, cc].each { |p| pkgmgr.register(p) }
      assert_equal [cc_name], Planner.graph(pkgmgr, pkgmgr.scope)["t"]
      assert_equal [cc_name, "t"], plan([["t", nil]]).builds.map(&:name)
    end
  end

  def test_a_name_that_is_no_package_is_refused_not_planned
    with_fake_tc do
      chain
      FileUtils.mkdir_p(noarch_pkgs / "ghost" / "1.0")   # an orphan
      r = plan([["ghost", nil]], force: true)
      assert_kind_of Refusal, r
      assert_match(/Package not found: ghost/, r.message)
    end
  end

  # The mark an upgrade inherits comes from a DEFAULT install that is
  # the user's and whole: a pinned one, an automatic one and a broken
  # one say nothing.
  def test_an_inherited_mark_comes_only_from_a_whole_manual_default
    with_fake_tc do
      t = FakePackage.new("t")
      t.define_singleton_method(:expected_files) { |v = nil|
        [["bin/t", false]]
      }
      pkgmgr.register(t)
      one = Ver("1.0.0")

      dir = fake_install(t, one, origin: :default, mark: :manual)
      assert_equal :manual, Planner.inherited_mark(pkgmgr.world, "t")

      FileUtils.rm_f(dir / "bin" / "t")                   # broken now
      pkgmgr.installs_changed!
      assert_equal :auto, Planner.inherited_mark(pkgmgr.world, "t")

      FileUtils.rm_rf(dir)
      fake_install(t, one, origin: :pinned, mark: :manual)
      assert_equal :auto, Planner.inherited_mark(pkgmgr.world, "t")

      FileUtils.rm_rf(dir)
      fake_install(t, one, origin: :default, mark: :auto)
      assert_equal :auto, Planner.inherited_mark(pkgmgr.world, "t")
    end
  end

  # --- -u, --mark, --autoremove: on values -----------------------------------

  def uninstall(name, **kw)
    return Planner.plan_uninstall(pkgmgr, world_now, name, pkgmgr.scope, **kw)
  end

  def test_uninstall_takes_the_default_here_else_everything_here
    with_fake_tc do
      t = FakePackage.new("t")
      t.define_singleton_method(:installable_versions) {
        [Ver("1.0.0"), Ver("2.0.0")]
      }
      pkgmgr.register(t)
      fake_install(t, Ver("2.0.0"))
      vers = ->(pl) { pl.removes.map { |r| r.install.ver } }
      assert_equal [Ver("2.0.0")], vers.call(uninstall("t")),
                   "the default is not here: everything here goes"
      fake_install(t, Ver("1.0.0"))
      assert_equal [Ver("1.0.0")], vers.call(uninstall("t")),
                   "the default is here: only it goes"
      assert_empty uninstall("t").notes, "a match has nothing to say"
      assert_equal 2, uninstall("t", ver: "ALL").removes.length
    end
  end

  def test_a_version_that_is_not_here_removes_nothing_and_says_so
    with_fake_tc do
      t = FakePackage.new("t")
      pkgmgr.register(t)
      fake_install(t)
      p = uninstall("t", ver: Ver("9.9.9"))
      assert_empty p.removes
      assert_match(/9.9.9 is not installed at these coordinates/,
                   p.notes.join("\n"))
      refute_match(/nothing matched/, p.notes.join("\n"))
    end
  end

  def test_nothing_matched_names_where_it_is
    with_fake_tc do
      t = FakePackage.new("t", arch_list: [I386, RV])
      pkgmgr.register(t)
      fake_install(t, at: t.at(pkgmgr.scope.with(arch: RV)).coords)
      p = uninstall("t")                       # the scope's arch: i386
      assert_empty p.removes
      assert_match(/t: nothing matched/, p.notes.join("\n"))
      assert_match(/installed at tilck-riscv64/, p.notes.join("\n"))
    end
  end

  def test_all_spares_the_compilers_unless_forced_and_never_the_interpreter
    with_fake_tc do
      t = FakePackage.new("t")
      cc = FakePackage.new("gcc-#{ARCH.name}-musl", on_host: true,
                           is_compiler: true, host_tier: :portable,
                           arch_list: ALL_HOST_ARCHS.values,
                           target_arch: ARCH)
      ruby = FakePackage.new("ruby")     # the name is what is spared
      [t, cc, ruby].each { |p| pkgmgr.register(p) }
      [t, cc, ruby].each { |p| fake_install(p) }
      names = ->(pl) { pl.removes.map { |r| r.install.pkgname }.sort }
      assert_equal ["t"], names.call(uninstall("ALL"))
      assert_equal [cc.name, "t"], names.call(uninstall("ALL", force: true))
      assert_equal [cc.name], names.call(uninstall("ALL", force: true,
                                                   except: ["t"]))
      assert_empty uninstall("ALL").notes, "ALL that matched says nothing"
    end
  end

  # `-u ALL` on a clean tree is a no-op by design, and so is --clean:
  # neither says "nothing matched".
  def test_all_on_an_empty_tree_says_nothing
    with_fake_tc do
      chain
      p = uninstall("ALL")
      assert_empty p.removes
      assert_empty p.notes
      c = Planner.plan_clean(pkgmgr, world_now, pkgmgr.scope)
      assert_empty c.removes
      assert_empty c.notes
    end
  end

  # --clean is every arch: without -a ALL, ALL means the scope's arch.
  def test_clean_takes_every_arch_where_all_takes_the_scopes
    with_fake_tc do
      t = FakePackage.new("t", arch_list: [I386, RV])
      pkgmgr.register(t)
      fake_install(t, at: t.at(pkgmgr.scope.with(arch: I386)).coords)
      fake_install(t, at: t.at(pkgmgr.scope.with(arch: RV)).coords)
      assert_equal 1, uninstall("ALL").removes.length
      c = Planner.plan_clean(pkgmgr, world_now, pkgmgr.scope)
      assert_equal 2, c.removes.length
    end
  end

  def test_mark_is_the_same_selection_re_marked
    with_fake_tc do
      _, b, c = chain
      fake_install(c, mark: :auto)
      fake_install(b, mark: :manual)
      p = Planner.plan_mark(pkgmgr, world_now, "c", true, pkgmgr.scope)
      assert_equal ["c"], p.marks.map { |m| m.install.pkgname }
      assert p.marks.first.manual
      assert_empty p.notes, "a match has nothing to say"
      gone = Planner.plan_mark(pkgmgr, world_now, "c", true, pkgmgr.scope,
                               ver: Ver("9.9.9"))
      assert_empty gone.marks
      assert_match(/9.9.9 is not installed/, gone.notes.join("\n"))
      refute_match(/nothing matched/, gone.notes.join("\n"))
      p2 = Planner.plan_mark(pkgmgr, world_now, "a", false, pkgmgr.scope)
      assert_empty p2.marks
      assert_match(/a: nothing matched, so nothing was marked/,
                   p2.notes.join("\n"))
    end
  end

  # A host install is not built with a cross compiler, so it does not
  # hold one; a target install does.
  def test_only_a_target_install_needs_the_cross_compiler
    with_fake_tc do
      t = FakePackage.new("t")
      h = FakePackage.new("host_h", **HOST)
      cc = FakePackage.new("gcc-#{ARCH.name}-musl", on_host: true,
                           is_compiler: true, host_tier: :portable,
                           arch_list: ALL_HOST_ARCHS.values,
                           target_arch: ARCH)
      [t, h, cc].each { |p| pkgmgr.register(p) }
      [t, h, cc].each { |p| fake_install(p) }
      installs, needs, = Planner.install_graph(pkgmgr, world_now,
                                               pkgmgr.scope)
      by = installs.to_h { |i| [i.pkgname, i] }
      assert_equal [cc.name], needs[by["t"]].map(&:pkgname)
      assert_empty needs[by["host_h"]]
    end
  end

  # Unusable travels the whole chain whatever order the installs are
  # visited in: top needs mid needs base needs what is gone, and top
  # is visited first.
  def test_unusable_reaches_the_end_of_a_chain_visited_top_first
    with_fake_tc do
      gone = FakePackage.new("gone")
      base = FakePackage.new("base", dep_list: [Dep("gone", false)])
      mid  = FakePackage.new("mid",  dep_list: [Dep("base", false)])
      top  = FakePackage.new("top",  dep_list: [Dep("mid", false)])
      [top, mid, base, gone].each { |p| pkgmgr.register(p) }
      [top, mid, base].each { |p| fake_install(p) }
      bad = Planner.unusable(*Planner.install_graph(pkgmgr, world_now,
                                                    pkgmgr.scope))
      words = bad.to_h { |i, w| [i.pkgname, w] }
      assert_equal({ "base" => ["gone 1.0.0"], "mid" => ["base 1.0.0"],
                     "top" => ["mid 1.0.0"] }, words,
                   "each says what it waits for: the gone, or the unusable")
    end
  end

  # What nothing manual holds goes, dependents before what they need.
  def test_autoremove_takes_the_unheld_dependents_first
    with_fake_tc do
      a, b, c = chain
      fake_install(c, mark: :auto)
      fake_install(b, mark: :auto)
      fake_install(a, mark: :auto)
      p = Planner.plan_autoremove(pkgmgr, world_now, pkgmgr.scope)
      assert_equal %w[a b c], p.removes.map { |r| r.install.pkgname }

      InstallOrigin.write(a.install_dir(a.default_ver), true, true)
      pkgmgr.installs_changed!
      p2 = Planner.plan_autoremove(pkgmgr, world_now, pkgmgr.scope)
      assert_empty p2.removes, "a manual root holds its closure"
      assert_match(/Nothing to remove/, p2.notes.join("\n"))
    end
  end

  # --- the plan is a value of its arguments ----------------------------------

  def test_the_same_arguments_give_the_same_plan
    with_fake_tc do
      a, _, c = chain
      fake_install(c)
      w = world_now
      p1 = Planner.plan_install(pkgmgr, w, [["a", nil]], pkgmgr.scope)
      p2 = Planner.plan_install(pkgmgr, w, [["a", nil]], pkgmgr.scope)
      assert_equal p1, p2
      assert_equal p1.builds.map(&:name), %w[b a]
    end
  end
end
