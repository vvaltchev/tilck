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
    return Planner.plan_install(pkgmgr, world_now, requested, scope,
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
      pkgmgr.default_stack = Ver("7.7.7")

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
      assert_equal [cc_name], Planner.graph(pkgmgr, scope)["t"]
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
    return Planner.plan_uninstall(pkgmgr, world_now, name, scope, **kw)
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
      fake_install(t, at: t.at(scope.with(arch: RV)).coords)
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
      c = Planner.plan_clean(pkgmgr, world_now, scope)
      assert_empty c.removes
      assert_empty c.notes
    end
  end

  # --clean is every arch: without -a ALL, ALL means the scope's arch.
  def test_clean_takes_every_arch_where_all_takes_the_scopes
    with_fake_tc do
      t = FakePackage.new("t", arch_list: [I386, RV])
      pkgmgr.register(t)
      fake_install(t, at: t.at(scope.with(arch: I386)).coords)
      fake_install(t, at: t.at(scope.with(arch: RV)).coords)
      assert_equal 1, uninstall("ALL").removes.length
      c = Planner.plan_clean(pkgmgr, world_now, scope)
      assert_equal 2, c.removes.length
    end
  end

  def test_mark_is_the_same_selection_re_marked
    with_fake_tc do
      _, b, c = chain
      fake_install(c, mark: :auto)
      fake_install(b, mark: :manual)
      p = Planner.plan_mark(pkgmgr, world_now, "c", true, scope)
      assert_equal ["c"], p.marks.map { |m| m.install.pkgname }
      assert p.marks.first.manual
      assert_empty p.notes, "a match has nothing to say"
      gone = Planner.plan_mark(pkgmgr, world_now, "c", true, scope,
                               ver: Ver("9.9.9"))
      assert_empty gone.marks
      assert_match(/9.9.9 is not installed/, gone.notes.join("\n"))
      refute_match(/nothing matched/, gone.notes.join("\n"))
      p2 = Planner.plan_mark(pkgmgr, world_now, "a", false, scope)
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
                                               scope)
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
                                                    scope))
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
      p = Planner.plan_autoremove(pkgmgr, world_now, scope)
      assert_equal %w[a b c], p.removes.map { |r| r.install.pkgname }

      ba = bound(a)
      InstallOrigin.write(ba.install_dir(ba.default_ver), true, true)
      pkgmgr.installs_changed!
      p2 = Planner.plan_autoremove(pkgmgr, world_now, scope)
      assert_empty p2.removes, "a manual root holds its closure"
      assert_match(/Nothing to remove/, p2.notes.join("\n"))
    end
  end

  # --- upgrades, staleness, rebuilds: on values ------------------------------

  def two_versions(name, **kw)
    p = FakePackage.new(name, **kw)
    p.define_singleton_method(:installable_versions) {
      [Ver("1.0.0"), Ver("2.0.0")]
    }
    p.define_singleton_method(:default_ver) { Ver("2.0.0") }
    pkgmgr.register(p)
    return p
  end

  def judged = world_now.judged(pkgmgr, scope)

  # An install made as the default whose default moved wants the new
  # one; a pinned install is left alone.
  def test_upgradable_is_a_default_install_whose_default_moved
    with_fake_tc do
      t = two_versions("t")
      fake_install(t, Ver("1.0.0"), origin: :default, mark: :manual)
      assert_equal ["t"], Planner.upgradable(pkgmgr, world_now,
                                             scope).map(&:name)
      p = Planner.plan_upgrade(pkgmgr, world_now, scope)
      assert_equal [["t", Ver("2.0.0"), :manual]],
                   p.builds.map { |b| [b.name, b.ver, b.mark] },
                   "the new version inherits the old one's mark"

      FileUtils.rm_rf(bound(t).install_dir(Ver("1.0.0")))
      fake_install(t, Ver("1.0.0"), origin: :pinned)
      assert_empty Planner.upgradable(pkgmgr, world_now, scope)
      p = Planner.plan_upgrade(pkgmgr, world_now, scope)
      assert_empty p.builds
      assert_match(/up to date/, p.notes.join("\n"))
    end
  end

  # A judged world carries each install's record; stale is what does
  # not read :ok, dependencies first, and a bumped version is not stale.
  def test_stale_installs_read_the_judged_world_dependencies_first
    with_fake_tc do
      a, b, c = chain
      fake_install(c, record: :changed)
      fake_install(b)
      fake_install(a, record: :missing)
      w = judged
      assert_equal %i[changed ok unknown],
                   %w[c b a].map { |n| w.of(n).first.record }
      stale = Planner.stale_installs(pkgmgr, w, scope)
      assert_equal %w[c a], stale.map { |p, _| p.name }
      rc, lines = Planner.check_updates(pkgmgr, w, scope)
      assert_equal 2, rc
      assert_equal ["NEEDS_REBUILD a c"], lines
    end
  end

  def test_an_unjudged_world_is_refused_not_misread
    with_fake_tc do
      _, _, c = chain
      fake_install(c)
      assert_raises(ArgumentError) {
        Planner.stale_installs(pkgmgr, world_now, scope)
      }
    end
  end

  # --rebuild replaces each stale install where it is, as it was asked
  # for, after any dependency the recipe has grown since.
  def test_rebuild_replaces_in_place_after_the_grown_dependency
    with_fake_tc do
      grown = FakePackage.new("grown")
      t = FakePackage.new("t")
      [grown, t].each { |p| pkgmgr.register(p) }
      fake_install(t, record: :changed, origin: :pinned, mark: :auto)
      t.define_singleton_method(:dep_list) { [Dep("grown", false)] }

      p = Planner.plan_rebuild(pkgmgr, judged, scope)
      assert_equal %w[grown], p.actions.grep(Build).map(&:name)
      r = p.replaces.first
      assert_equal "t", r.install.pkgname
      assert_equal [Ver("1.0.0"), :pinned, :auto],
                   [r.build.ver, r.build.origin, r.build.mark]
      assert_match(/Installs to rebuild/, p.notes.join("\n"))
      assert_equal p.actions.last, r, "the replace comes after the grown dep"
    end
  end

  def test_rebuild_refuses_an_install_whose_deps_cannot_be_known
    with_fake_tc do
      d = two_versions("host_d", **HOST)
      u = FakePackage.new("host_u", **HOST, dep_list: [Dep("host_d", true)])
      pkgmgr.register(u)
      fake_install(d, Ver("1.0.0"))
      fake_install(d, Ver("2.0.0"))
      fake_install(u, record: :changed)      # a fake install has no record
      r = Planner.plan_rebuild(pkgmgr, judged, scope)
      assert_kind_of Refusal, r
      assert_match(/host_u:1.0.0 has no record of which host_d/, r.message)
    end
  end

  # ...at a board the package no longer builds for, while it still
  # builds for the invocation's.
  def test_rebuild_leaves_an_install_its_package_cannot_build_here
    with_fake_tc do
      with_context(ARCH: RV, BOARD: "qemu-virt") do   # inside: the fake
        t = FakePackage.new("t", arch_list: [RV])     # tc sets ARCH too
        pkgmgr.register(t)
        nano = scope.with(arch: RV, board: "licheerv-nano")
        fake_install(t, at: t.at(nano).coords, record: :changed)
        t.instance_variable_set(:@board_list, ["qemu-virt"])  # dropped
        p = Planner.plan_rebuild(pkgmgr, judged, scope)
        assert_empty p.actions
        assert_match(/Left as it is: t:1.0.0 .*does not build for board/,
                     p.notes.join("\n"))
        refute_match(/Installs to rebuild/, p.notes.join("\n"),
                     "nothing is rebuilt, and the plan does not say so")
      end
    end
  end

  # A grown dependency two stale installs share is built once.
  def test_rebuild_builds_a_shared_grown_dependency_once
    with_fake_tc do
      grown = FakePackage.new("grown")
      a = FakePackage.new("a")
      b = FakePackage.new("b")
      [grown, a, b].each { |p| pkgmgr.register(p) }
      [a, b].each { |p| fake_install(p, record: :changed) }
      [a, b].each { |p|
        p.define_singleton_method(:dep_list) { [Dep("grown", false)] }
      }
      p = Planner.plan_rebuild(pkgmgr, judged, scope)
      assert_equal ["grown"], p.actions.grep(Build).map(&:name)
      assert_equal %w[a b], p.replaces.map { |r| r.install.pkgname }.sort
    end
  end

  # An install recorded against one version of a dependency that its
  # recipe now pins to another cannot be planned: a conflict, refused.
  def test_rebuild_refuses_a_record_that_conflicts_with_the_recipe
    with_fake_tc do
      d = two_versions("host_d", **HOST)
      u = FakePackage.new("host_u", **HOST,
                          dep_list: [Dep("host_d", true, ver: Ver("2.0.0"))])
      pkgmgr.register(u)
      fake_install(d, Ver("1.0.0"))
      inst = fake_install(u, record: :changed)
      InstallDeps.write(inst, { "host_d" => Ver("1.0.0") })
      r = Planner.plan_rebuild(pkgmgr, judged, scope)
      assert_kind_of Refusal, r
      assert_match(/Version conflict/, r.message)
    end
  end

  # A stale install is rebuilt WHERE IT IS -- at its own stack -- even
  # when the compiler of that stack is gone and the request resolves
  # to another one: the install was there before, and nothing already
  # there moves. Found by the exhaustive lane (stack/124/2/51): the
  # request's stack was taken for the install's, which built a second
  # copy at the wrong stack, as pinned, and took the old one away.
  def test_rebuild_keeps_an_install_at_its_own_stack
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
      pkgmgr.default_stack = Ver("7.7.7")
      fake_install(gcc)
      fake_install(s, at: pkgmgr.stack_coords(Ver("8.8.8")),
                   record: :changed, mark: :auto)

      p = Planner.plan_rebuild(pkgmgr, judged, scope)
      assert_equal [Replace], p.actions.map(&:class)
      b = p.replaces.first.build
      assert_equal Ver("8.8.8"), b.scope.stack
      assert_equal [:default, :auto], [b.origin, b.mark]
    end
  end

  # ...and a dependency it has no record of, none of which is
  # installed, is taken at the REQUEST's default -- the stack in
  # effect for the stack compiler -- not at the install's: the
  # install's coordinates name a compiler nothing here can put back.
  # The model says the same (stack/7/0/51).
  def test_rebuild_takes_an_unrecorded_dependency_at_the_request_default
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
      pkgmgr.default_stack = Ver("7.7.7")
      fake_install(s, at: pkgmgr.stack_coords(Ver("8.8.8")),
                   record: :changed)

      p = Planner.plan_rebuild(pkgmgr, judged, scope)
      assert_equal [Build, Replace], p.actions.map(&:class)
      assert_equal ["host_gcc", Ver("7.7.7")],
                   [p.actions.first.name, p.actions.first.ver]
      assert_equal Ver("8.8.8"), p.replaces.first.build.scope.stack
    end
  end

  def test_installable_tags_defaults_and_the_rest
    with_fake_tc do
      d = FakePackage.new("dflt", default: true, dep_list: [Dep("lib", false)])
      lib = FakePackage.new("lib")
      opt = FakePackage.new("opt")
      [d, lib, opt].each { |p| pkgmgr.register(p) }
      list = Planner.installable(pkgmgr, scope)
      assert_equal [["lib", "default"], ["dflt", "default"],
                    ["opt", "optional"]], list
    end
  end

  # --- one request, and the world it leaves ----------------------------------

  def req(mode, targets = [], **kw)
    return Request.make(mode, targets: targets, **kw)
  end

  # `-a ALL` plans each arch from the world the arch before it leaves:
  # a noarch dependency is built under the first arch and found there
  # by the second.
  def test_step_threads_the_world_through_every_arch
    with_fake_tc do
      n = FakePackage.new("n", arch_list: nil)
      t = FakePackage.new("t", dep_list: [Dep("n", false)])
      [n, t].each { |p| pkgmgr.register(p) }

      out = Planner.step(pkgmgr, world_now, req(:install, [["t", nil]],
                                                arch: :all), scope)
      assert_equal 0, out.rc
      assert_equal ALL_ARCHS.values, out.acts.map(&:arch)
      builds = out.acts.map { |a| a.plan.builds.map(&:name) }
      assert_equal %w[n t], builds.first
      assert_equal %w[t], builds.last, "n was built under the first arch"
      assert_equal ALL_ARCHS.length + 1, out.world.installs.length
    end
  end

  # ...and an arch that cannot build a root is skipped and said, the
  # others still installed.
  def test_step_skips_an_arch_a_root_does_not_build_for
    with_fake_tc do
      pkgmgr.register(FakePackage.new("rv", arch_list: [RV]))
      out = Planner.step(pkgmgr, world_now, req(:install, [["rv", nil]],
                                                arch: :all), scope)
      assert_equal 0, out.rc
      skipped = out.acts.select { |a| a.plan.nil? }
      assert_equal ALL_ARCHS.length - 1, skipped.length
      assert_match(/Skipping rv: not supported on arch i386/,
                   skipped.first.notes.join)
      assert_equal 1, out.world.installs.length
    end
  end

  # A dry run plans and leaves the world as it was.
  def test_a_dry_step_leaves_the_world_as_it_was
    with_fake_tc do
      t = FakePackage.new("t")
      pkgmgr.register(t)
      before = world_now
      out = Planner.step(pkgmgr, before, req(:install, [["t", nil]],
                                             dry: true), scope)
      assert_equal 0, out.rc
      assert_equal %w[t], out.plans.first.builds.map(&:name)
      assert_equal before, out.world
    end
  end

  # What is refused, and how: an unknown name, a version the package
  # does not offer, a package the arch does not build, a short name
  # that is ambiguous. Nothing is planned.
  def test_step_refuses_at_the_door
    with_fake_tc do
      two_versions("t")
      pkgmgr.register(FakePackage.new("rv", arch_list: [RV]))
      pkgmgr.register(FakePackage.new("aa"))
      pkgmgr.register(FakePackage.new("ab"))

      out = Planner.step(pkgmgr, world_now, req(:install, [["nope", nil]]),
                         scope)
      assert_equal [1, "Package not found: nope"], [out.rc, out.message]
      assert_empty out.acts

      out = Planner.step(pkgmgr, world_now,
                         req(:install, [["t", Ver("9.9.9")]]), scope)
      assert_equal 1, out.rc
      assert_match(/t:9.9.9 is not a version t can install\nAvailable: /,
                   out.message)

      out = Planner.step(pkgmgr, world_now, req(:install, [["rv", nil]]),
                         scope)
      assert_equal [1, "Package rv is not supported for arch i386"],
                   [out.rc, out.message]

      out = Planner.step(pkgmgr, world_now, req(:uninstall, [["a", nil]]),
                         scope)
      assert_equal 1, out.rc
      assert_match(/Ambiguous package name 'a' matches: aa, ab/, out.message)
    end
  end

  # A short name resolves, and the resolution is said.
  def test_step_says_the_name_it_matched
    with_fake_tc do
      pkgmgr.register(FakePackage.new("longname"))
      out = Planner.step(pkgmgr, world_now, req(:install, [["long", nil]]),
                         scope)
      assert_equal 0, out.rc
      assert_equal ["Matched 'long' -> 'longname'"], out.acts.first.notes
      assert_equal %w[longname], out.plans.first.builds.map(&:name)
    end
  end

  # -u takes its targets one at a time, each from the world the one
  # before leaves, and an orphan by its name.
  def test_step_uninstalls_each_target_in_turn
    with_fake_tc do
      a = FakePackage.new("a")
      b = FakePackage.new("b")
      [a, b].each { |p| pkgmgr.register(p) }
      fake_install(a)
      fake_install(b)
      orphan = FakePackage.new("gone")
      pkgmgr.register(orphan)
      fake_install(orphan)
      pkgmgr.instance_variable_get(:@packages).delete("gone")
      pkgmgr.installs_changed!

      out = Planner.step(pkgmgr, world_now,
                         req(:uninstall, [["a", nil], ["gone", nil]]), scope)
      assert_equal 0, out.rc
      assert_equal 2, out.plans.length
      assert_equal %w[b], out.world.installs.map(&:pkgname)
    end
  end

  # The default install claims the stack's members and carries the
  # upgrades beside them; --contrib appends the extras when they are
  # registered.
  def test_step_default_installs_the_stack_and_the_upgrades
    with_fake_tc do
      register_tilck_stack!
      d = FakePackage.new("dflt", default: true)
      u = two_versions("u")
      pkgmgr.register(d)
      fake_install(u, Ver("1.0.0"))
      out = Planner.step(pkgmgr, world_now, req(:default), scope)
      assert_equal 0, out.rc
      act = out.acts.first
      assert_equal %w[u], act.upgrades
      marks = act.plan.builds.to_h { |b| [b.name, b.mark] }
      assert_equal :auto, marks.fetch("dflt"), "a member is a dependency"
      assert_equal :manual, marks.fetch(pkgmgr.tilck_stacks.first.name),
                   "the stack itself is claimed"
      assert_equal :manual, marks.fetch("u"),
                   "an upgrade inherits the mark of what it replaces"
    end
  end

  # -C plans nothing: it refuses what is not configurable, and a dry
  # run says what it would do.
  def test_step_configure_refuses_and_says
    with_fake_tc do
      pkgmgr.register(FakePackage.new("t"))
      out = Planner.step(pkgmgr, world_now, req(:configure, [["t", nil]]),
                         scope)
      assert_equal [1, "Package t does not support reconfiguration"],
                   [out.rc, out.message]
      out = Planner.step(pkgmgr, world_now, req(:configure, [["x", nil]]),
                         scope)
      assert_equal "Package not found: x", out.message
    end
  end

  # The observations change nothing; --check-for-updates answers its
  # exit code and its lines.
  def test_step_observations_leave_the_world_and_check_updates_reports
    with_fake_tc do
      t = two_versions("t")
      fake_install(t, Ver("1.0.0"))
      before = judged
      for mode in %i[list installable layout context other] do
        out = Planner.step(pkgmgr, before, req(mode), scope)
        assert_equal [0, before, []], [out.rc, out.world, out.acts]
      end
      out = Planner.step(pkgmgr, before, req(:check_updates), scope)
      assert_equal [2, ["NEEDS_UPGRADE t"]], [out.rc, out.notes]
      assert_equal before, out.world
      assert_nil out.message
    end
  end

  # Plan#apply: a build's install is what the scan reads, a removal
  # takes exactly one, a mark keeps the rest of the install as it is.
  def test_apply_makes_the_installs_the_plan_describes
    with_fake_tc do
      t = FakePackage.new("t", arch_list: [I386, RV])
      pkgmgr.register(t)
      i = fake_install(t, at: t.at(scope.with(arch: I386)).coords, mark: :auto)
      before = world_now
      inst = before.installs.first
      assert_equal i, inst.path

      built = Planner.plan_install(pkgmgr, before, [["t", nil]],
                                   scope.with(arch: RV)).apply(pkgmgr, before)
      assert_equal 2, built.installs.length
      made = built.installs.find { |x| x.arch == RV }
      assert_equal [t.at(scope.with(arch: RV)).install_dir(Ver("1.0.0")),
                    true, true, :ok, Ver("13.3.0")],
                   [made.path, made.default_install, made.manual, made.record,
                    made.compiler]

      marked = Plan.new(actions: [Mark.new(install: inst, manual: true)],
                        scope: scope, bound: {}, notes: [])
                   .apply(pkgmgr, before)
      assert_equal [true, inst.path], [marked.installs.first.manual,
                                       marked.installs.first.path]

      gone = Plan.new(actions: [Remove.new(install: inst)], scope: scope,
                      bound: {}, notes: []).apply(pkgmgr, built)
      assert_equal [RV], gone.installs.map(&:arch)
    end
  end

  # A name as typed: exact wins without a word; a short name is
  # matched and said; three candidates are listed whole, four with an
  # ellipsis.
  def test_resolve_name_says_only_what_it_changed
    with_fake_tc do
      %w[t tx ty tz].each { |n| pkgmgr.register(FakePackage.new(n)) }
      assert_equal ["t", nil], Planner.resolve_name(pkgmgr, "t")
      assert_equal ["tx", "Matched 'x' -> 'tx'"],
                   Planner.resolve_name(pkgmgr, "x")
      r = Planner.resolve_name(pkgmgr, "z")
      assert_equal ["tz", "Matched 'z' -> 'tz'"], r
      pkgmgr.register(FakePackage.new("ta"))
      pkgmgr.register(FakePackage.new("tb"))
      %w[tc].each { |n| pkgmgr.register(FakePackage.new(n)) }
      r = Planner.resolve_name(pkgmgr, "t")
      assert_equal ["t", nil], r, "exact wins over every substring"
      pkgmgr.instance_variable_get(:@packages).delete("t")
      r = Planner.resolve_name(pkgmgr, "t")
      assert_kind_of Refusal, r
      assert_match(/matches: tx, ty, tz, \.\.\.\z/, r.message)
      %w[tx ty tz].each { |n|
        pkgmgr.instance_variable_get(:@packages).delete(n)
      }
      r = Planner.resolve_name(pkgmgr, "t")
      assert_match(/matches: ta, tb, tc\z/, r.message)
    end
  end

  # The words the selector is handed for -a and -c.
  def test_the_selector_is_handed_words
    r = Request.make(:uninstall, cc: :all, arch: :all)
    assert_equal ["ALL", "ALL"], [Planner.cc_word(r), Planner.arch_word(r)]
    r = Request.make(:uninstall, cc: Ver("7.7.7"), arch: RV)
    assert_equal ["7.7.7", "riscv64"],
                 [Planner.cc_word(r), Planner.arch_word(r)]
    r = Request.make(:uninstall, cc: "syscc")
    assert_equal ["syscc", nil], [Planner.cc_word(r), Planner.arch_word(r)]
    assert_nil Planner.cc_word(Request.make(:uninstall))
  end

  # --contrib appends the extras that are registered, once.
  def test_step_default_appends_the_contrib_extras_once
    with_fake_tc do
      register_tilck_stack!
      names = ->(contrib) {
        out = Planner.step(pkgmgr, world_now, req(:default, contrib: contrib),
                           scope)
        assert_equal 0, out.rc
        out.acts.first.roots
      }
      refute_includes names.call(true), "host_mconf", "not registered: skipped"
      m = two_versions("host_mconf", **HOST)
      refute_includes names.call(false), "host_mconf"
      assert_equal 1, names.call(true).count("host_mconf")
      fake_install(m, Ver("1.0.0"))
      assert_equal 1, names.call(true).count("host_mconf"),
                   "an upgrade already in the set is not appended again"
    end
  end

  # A default install or an upgrade whose plan is refused says so and
  # plans nothing.
  def test_step_default_and_upgrade_pass_a_refusal_on
    with_fake_tc do
      register_tilck_stack!
      x = two_versions("host_x", **HOST)
      a = two_versions("a", default: true,
                       dep_list: [Dep("host_x", true, ver: Ver("1.0.0"))])
      b = two_versions("b", default: true,
                       dep_list: [Dep("host_x", true, ver: Ver("2.0.0"))])
      out = Planner.step(pkgmgr, world_now, req(:default), scope)
      assert_equal 1, out.rc
      assert_match(/Version conflict/, out.message)
      assert_empty out.acts

      fake_install(a, Ver("1.0.0"))
      fake_install(b, Ver("1.0.0"))
      out = Planner.step(pkgmgr, world_now, req(:upgrade), scope)
      assert_equal 1, out.rc
      assert_match(/Version conflict/, out.message)
      assert_empty out.acts
      assert_nil x.instance_variable_get(:@nothing)
    end
  end

  # A mark of what is not a package is refused like an uninstall.
  def test_step_mark_refuses_an_unknown_name
    with_fake_tc do
      out = Planner.step(pkgmgr, world_now, req(:mark_auto, [["nope", nil]]),
                         scope)
      assert_equal [1, "Package not found: nope", []],
                   [out.rc, out.message, out.acts]
    end
  end

  # A dry -C names the version it would reconfigure: the one asked
  # for, else the default.
  def test_step_configure_dry_names_the_version
    with_fake_tc do
      t = two_versions("t")
      t.define_singleton_method(:configurable?) { true }
      out = Planner.step(pkgmgr, world_now, req(:configure, [["t", nil]],
                                                dry: true), scope)
      assert_equal 0, out.rc
      assert_nil out.acts.first.plan
      assert_equal ["t", nil], out.acts.first.roots
      assert_match(/would reconfigure t 2.0.0,/, out.notes.join)
      out = Planner.step(pkgmgr, world_now,
                         req(:configure, [["t", Ver("1.0.0")]], dry: true),
                         scope)
      assert_match(/would reconfigure t 1.0.0,/, out.notes.join)
      assert_equal ["t", Ver("1.0.0")], out.acts.first.roots
      out = Planner.step(pkgmgr, world_now, req(:configure, [["t", nil]]),
                         scope)
      assert_equal [0, []], [out.rc, out.notes]
    end
  end

  # An outcome that went through has no message.
  def test_an_outcome_that_went_through_has_no_message
    with_fake_tc do
      pkgmgr.register(FakePackage.new("t"))
      out = Planner.step(pkgmgr, world_now, req(:install, [["t", nil]]), scope)
      assert_nil out.message
      assert_nil Outcome.ok(world_now).message
    end
  end

  # --- the plan is a value of its arguments ----------------------------------

  def test_the_same_arguments_give_the_same_plan
    with_fake_tc do
      a, _, c = chain
      fake_install(c)
      w = world_now
      p1 = Planner.plan_install(pkgmgr, w, [["a", nil]], scope)
      p2 = Planner.plan_install(pkgmgr, w, [["a", nil]], scope)
      assert_equal p1, p2
      assert_equal p1.builds.map(&:name), %w[b a]
    end
  end
end
