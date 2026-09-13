# SPDX-License-Identifier: BSD-2-Clause
#
# THE EXECUTOR, JUDGED BY THE PLAN.
#
# The only lane that touches the disk for the sake of logic. For each
# kind of action -- Build, Remove, Mark, Replace -- one plan is run on
# a fake tree, and the tree read back (World.scan, judged) must equal
# the world the plan says it leaves (Plan#apply): every install, every
# field, the path and the record included. The exhaustive lane
# compares the planner with the model on values alone; this is what
# makes those values mean the tree.
#

require_relative 'test_helper'
require_relative 'model/bridge'

class TestExecutor < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]

  def setup
    reset_pkgmgr!
  end

  # The tree as it is, judged: what the executor left.
  def tree = World.scan(pkgmgr.all_packages).judged(pkgmgr, scope)

  # The world a plan starts from, cut loose from the tree.
  def world_now = World.of(tree.installs)

  # Same installs (identity: name, version, coordinates, path, origin,
  # mark, ...) and the same records.
  def assert_same_world(want, got, what)
    assert_equal want.installs.to_set, got.installs.to_set,
                 "#{what}: the tree is not what the plan said"
    assert_equal Bridge.keys_of_world(want), Bridge.keys_of_world(got),
                 "#{what}: the records are not what the plan said"
  end

  def run_plan(plan)
    with_stubbed_externals { Executor.run(pkgmgr, plan) }
  end

  # A target, a noarch and a host build: each kind reads back as the
  # plan said.
  def test_a_build_leaves_the_install_the_plan_says
    with_fake_tc do
      n = FakePackage.new("n", arch_list: nil)
      h = FakePackage.new("host_h", on_host: true, host_tier: :portable,
                          arch_list: ALL_HOST_ARCHS.values)
      b = FakePackage.new("b", dep_list: [Dep("n", false),
                                          Dep("host_h", false)])
      a = FakePackage.new("a", dep_list: [Dep("b", false)])
      [a, b, n, h].each { |p| pkgmgr.register(p) }
      before = world_now
      plan = Planner.plan_install(pkgmgr, before, [["a", nil]], scope)
      assert_equal %w[a b host_h n], plan.builds.map(&:name).sort
      assert_nil run_plan(plan)
      assert_same_world(plan.apply(pkgmgr, before), tree, "build")
      assert_equal 4, tree.installs.length
    end
  end

  def test_a_forced_build_removes_and_leaves_what_the_plan_says
    with_fake_tc do
      t = FakePackage.new("t")
      pkgmgr.register(t)
      fake_install(t, mark: :auto)
      before = world_now
      plan = Planner.plan_install(pkgmgr, before, [["t", nil]], scope,
                                  force: true)
      assert_equal [Remove, Build], plan.actions.map(&:class)
      assert_nil run_plan(plan)
      assert_same_world(plan.apply(pkgmgr, before), tree, "forced build")
    end
  end

  def test_a_removal_leaves_what_the_plan_says
    with_fake_tc do
      t = FakePackage.new("t", arch_list: [I386, RV])
      pkgmgr.register(t)
      fake_install(t, at: t.at(scope.with(arch: I386)).coords)
      fake_install(t, at: t.at(scope.with(arch: RV)).coords)
      before = world_now
      plan = Planner.plan_uninstall(pkgmgr, before, "t", scope)
      assert_equal 1, plan.removes.length
      assert_nil run_plan(plan)
      assert_same_world(plan.apply(pkgmgr, before), tree, "removal")
      assert_equal 1, tree.installs.length
    end
  end

  def test_a_mark_leaves_what_the_plan_says
    with_fake_tc do
      t = FakePackage.new("t")
      pkgmgr.register(t)
      fake_install(t, mark: :manual)
      before = world_now
      plan = Planner.plan_mark(pkgmgr, before, "t", false, scope)
      assert_equal 1, plan.marks.length
      assert_nil run_plan(plan)
      assert_same_world(plan.apply(pkgmgr, before), tree, "mark")
      refute tree.installs.first.manual
    end
  end

  def test_a_replace_leaves_what_the_plan_says
    with_fake_tc do
      t = FakePackage.new("t")
      pkgmgr.register(t)
      fake_install(t, record: :changed, origin: :pinned, mark: :auto)
      before = world_now
      assert_equal [:changed], before.installs.map(&:record)
      plan = Planner.plan_rebuild(pkgmgr, before, scope)
      assert_equal [Replace], plan.actions.map(&:class)
      assert_nil run_plan(plan)
      assert_same_world(plan.apply(pkgmgr, before), tree, "replace")
      assert_equal [:ok], tree.installs.map(&:record)
    end
  end

  # A package that writes several installs from one build -- gnuefi
  # builds for two arches and noarch -- and installs itself whole:
  # every one of its installs gets its origin, its mark and its
  # record, and the tree reads back as the plan said, marks included.
  def test_a_build_that_writes_several_installs_records_each
    with_fake_tc do
      m = FakePackage.new("multi", arch_list: [I386, RV])
      m.define_singleton_method(:install_archs) { |ver = nil| [I386, RV] }
      m.define_singleton_method(:install_impl) { |ver|
        for a in [I386, RV] do
          FileUtils.mkdir_p(at(scope.with(arch: a)).install_dir(ver))
        end
        true
      }
      pkgmgr.register(m)
      before = world_now
      plan = Planner.plan_install(pkgmgr, before, [["multi", nil]], scope,
                                  claimed: [])
      assert_equal :auto, plan.builds.first.mark, "unclaimed: auto"
      assert_nil run_plan(plan)
      assert_same_world(plan.apply(pkgmgr, before), tree, "multi-arch build")
      assert_equal [false, false], tree.installs.map(&:manual)
      assert_equal [:ok, :ok], tree.installs.map(&:record)
    end
  end

  # A plan with nothing in it leaves the tree alone.
  def test_an_empty_plan_leaves_the_tree_as_it_was
    with_fake_tc do
      t = FakePackage.new("t")
      pkgmgr.register(t)
      fake_install(t)
      before = world_now
      plan = Plan.new(actions: [], scope: scope, bound: {}, notes: [])
      assert_nil run_plan(plan)
      assert_same_world(before, tree, "empty plan")
    end
  end
end
