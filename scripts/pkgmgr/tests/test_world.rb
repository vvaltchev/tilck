# SPDX-License-Identifier: BSD-2-Clause
#
# THE WORLD: what is installed, as one value.
#
# The instrument first: a scan must equal a second scan of the same
# tree, or no comparison against it means anything. Then what the
# value answers -- a package's installs, the one at some coordinates,
# what nobody claims -- and that a world built in memory from the
# same installs answers the same, which is what lets the planner be
# tested without a tree at all.
#

require_relative 'test_helper'

class TestWorld < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]

  def two_installs
    reset_pkgmgr!
    pkg = FakePackage.new("t", arch_list: [I386, RV])
    pkgmgr.register(pkg)
    fake_install(pkg, at: pkg.at(pkgmgr.scope.with(arch: I386)).coords)
    fake_install(pkg, at: pkg.at(pkgmgr.scope.with(arch: RV)).coords)
    return pkg
  end

  # --- the instrument -----------------------------------------------------

  def test_a_scan_equals_a_second_scan_of_the_same_tree
    with_fake_tc do
      two_installs
      a = World.scan(pkgmgr.all_packages)
      b = World.scan(pkgmgr.all_packages)
      assert_equal a, b
      assert_equal 2, a.installs.length
    end
  end

  def test_two_readings_of_one_install_are_equal
    with_fake_tc do
      pkg = two_installs
      a, b = World.scan([pkg]).installs, World.scan([pkg]).installs
      assert_equal a, b
      refute_same a.first, b.first
      assert_equal a.first.hash, b.first.hash
    end
  end

  # --- what it answers -----------------------------------------------------

  def test_of_find_and_orphans
    with_fake_tc do
      pkg = two_installs
      FileUtils.mkdir_p(noarch_pkgs / "gone_pkg" / "1.0")
      w = World.scan(pkgmgr.all_packages)

      assert_equal 2, w.of("t").length
      assert_empty w.of("gone_pkg"), "an orphan is not a package's install"
      assert_equal ["gone_pkg"], w.orphans.map(&:pkgname)
      assert_equal 3, w.installs.length
      assert_equal w.claimed + w.orphans, w.installs

      want = pkg.at(pkgmgr.scope.with(arch: RV)).coords
      found = w.find("t", pkg.default_ver, want)
      assert_equal want, found.coords
      assert_nil w.find("t", Ver("9.9.9"), want)
      assert_nil w.find("t", pkg.default_ver,
                        Coords.new("tilck-x86_64", "pc", "gcc-1.0"))
    end
  end

  def test_a_broken_install_is_listed_but_never_found
    with_fake_tc do
      reset_pkgmgr!
      pkg = FakePackage.new("t")
      pkg.define_singleton_method(:expected_files) { |v = nil|
        [["bin/t", false]]
      }
      pkgmgr.register(pkg)
      dir = pkg.install_dir(pkg.default_ver)
      FileUtils.mkdir_p(dir)                     # no bin/t: broken
      w = World.scan([pkg])
      assert_equal 1, w.of("t").length
      assert w.of("t").first.broken
      assert_nil w.find("t", pkg.default_ver, pkg.coords)
    end
  end

  # A world built from the installs a scan found answers exactly as
  # the scan does: the planner can be handed one with no tree behind it.
  def test_a_world_built_in_memory_answers_as_the_scanned_one
    with_fake_tc do
      pkg = two_installs
      scanned = World.scan([pkg])
      built = World.of(scanned.installs)
      assert_equal scanned.installs, built.installs
      want = pkg.at(pkgmgr.scope.with(arch: I386)).coords
      assert_equal scanned.find("t", pkg.default_ver, want),
                   built.find("t", pkg.default_ver, want)
    end
  end

  # --- the manager's one copy ------------------------------------------

  # Held until a writer says the tree changed, then read again.
  def test_the_manager_scans_once_per_change
    with_fake_tc do
      reset_pkgmgr!
      pkg = FakePackage.new("t")
      pkgmgr.register(pkg)
      before = pkgmgr.world
      assert_same before, pkgmgr.world, "no change, no rescan"
      assert_empty before.of("t")

      fake_install(pkg)                          # announces the change
      after = pkgmgr.world
      refute_same before, after
      assert_equal 1, after.of("t").length
      assert_equal 1, pkg.get_install_list.length
      assert pkg.installed?(pkg.default_ver)
    end
  end

  # A package bound with a world reads that world, whatever the
  # manager holds.
  def test_a_package_bound_with_a_world_reads_that_world
    with_fake_tc do
      reset_pkgmgr!
      pkg = FakePackage.new("t")
      pkgmgr.register(pkg)
      fake_install(pkg)
      empty = World.empty
      b = pkg.at(pkgmgr.scope, world: empty)
      refute b.installed?(pkg.default_ver)
      assert pkg.installed?(pkg.default_ver), "the registry package: manager's"
      assert_raises(ArgumentError) { pkg.at(pkgmgr.scope, world: []) }
    end
  end
end
