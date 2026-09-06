# SPDX-License-Identifier: BSD-2-Clause
#
# Each QEMU is built by a compiler from its own time.
#
# The pairing is a declared table, so it is a second copy of a fact
# about the world -- and the failure mode of getting it wrong is not a
# build error but a build that succeeds against a compiler five years
# newer than the sources, which is a test of neither.
#
# What can be checked here is that the table is COHERENT: every series
# covered, every compiler one we can actually build, and the pin
# reaching the solver rather than sitting in a constant nobody reads.
#

require_relative 'test_helper'
require_relative '../main'

class TestQemuStack < Minitest::Test

  include TestHelper

  # Snapshotted at LOAD time: reset_pkgmgr! empties the registry for
  # whichever test wants a clean one, and the order is randomised.
  PACKAGES = pkgmgr.all_packages.dup.freeze

  def pkg(name) = PACKAGES.find { |p| p.name == name }
  def qemu = pkg("host_qemu")

  # The solver reads the REGISTRY, not a snapshot, and another test
  # class's reset_pkgmgr! may have emptied it before this one runs.
  def setup
    reset_pkgmgr!
    PACKAGES.each { |p| pkgmgr.register(p) }
  end

  def test_every_supported_version_has_a_compiler
    missing = HostQemuPackage::SUPPORTED.reject { |v|
      HostQemuPackage::GCC_FOR.key?(v.series)
    }
    assert_empty missing.map(&:to_s), "QEMU versions with no compiler"
  end

  # A pin naming a compiler we cannot build is a stack that can never
  # exist, and the run would only find out after resolving.
  def test_every_pinned_compiler_can_be_built
    gcc = pkg("host_gcc")

    for series, ver in HostQemuPackage::GCC_FOR do
      assert_includes gcc.installable_versions, ver,
                      "QEMU #{series}.x asks for GCC #{ver}, " \
                      "which host_gcc cannot build"
    end
  end

  # One compiler per series and no sharing: two QEMUs in one stack
  # would defeat the point of pinning at all.
  def test_each_series_gets_its_own_compiler
    vers = HostQemuPackage::GCC_FOR.values
    assert_equal vers.length, vers.uniq.length,
                 "two QEMU series share a compiler"
  end

  # Newer QEMU, newer compiler. A table written by hand can invert a
  # pair without looking wrong.
  def test_the_table_runs_the_same_way_as_time
    pairs = HostQemuPackage::GCC_FOR.sort_by { |series, _| series }
    assert_equal pairs.map { |_, v| v }, pairs.map { |_, v| v }.sort,
                 "a newer QEMU is pinned to an older compiler"
  end

  def test_the_pin_reaches_the_dependency_list
    for v in HostQemuPackage::SUPPORTED do
      dep = qemu.dep_list_for(v).find { |d| d.name == "host_gcc" }
      refute_nil dep, "host_gcc left out of the deps for #{v}"
      assert_equal HostQemuPackage::GCC_FOR[v.series], dep.ver
    end
  end

  # Replaced, not appended: the same package named twice, once bare
  # and once pinned, leaves the winner to the solver's walk order.
  def test_host_gcc_appears_once
    deps = qemu.dep_list_for(Ver("9.2.0")).select { |d|
      d.name == "host_gcc"
    }
    assert_equal 1, deps.length
  end

  # The point of the pin: HOST_VER_GCC sets the default for packages
  # that do not care which compiler builds them. QEMU cares, so the
  # default must not reach it -- with HOST_VER_GCC naming 14.4.0,
  # asking for QEMU 7 still builds the gcc-12.5.0 world.
  def test_the_pin_beats_the_default_stack
    assert_equal Ver("14.4.0"), pkgmgr.default_stack_cc_ver,
                 "this test assumes the tree's default is 14.4.0"

    for v, want in { "7.2.0" => "12.5.0", "11.1.0" => "16.2.0" } do
      stack = pkgmgr.resolved_versions_for([["host_qemu", Ver(v)]])["host_gcc"]

      assert_equal Ver(want), stack,
                   "QEMU #{v} resolved to the wrong compiler"
      refute_equal pkgmgr.default_stack_cc_ver, stack,
                   "the default reached a package that pins its own"
    end
  end

  # ...and the stack the install lands in follows that, since it is
  # the resolved host_gcc version the install scope is opened with.
  def test_the_install_goes_into_the_pinned_stack
    stack = pkgmgr.resolved_versions_for(
      [["host_qemu", Ver("7.2.0")]]
    )["host_gcc"]

    coords = pkgmgr.with_host_stack(stack) { qemu.coords }
    assert_equal "gcc-12.5.0", coords.stack
  end

  # --- configure options that upstream removed ---------------------

  # QEMU stops on an option it does not know, so an option removed
  # upstream must stop being passed. glusterfs is a feature in 11.0
  # and gone in 11.1, which is why the boundary is a version rather
  # than a series.
  def test_glusterfs_is_passed_only_while_it_exists
    for v in ["6.2.0", "9.2.0", "10.2.0", "11.0.0"] do
      assert_includes qemu.configure_flags(Ver(v)), "--disable-glusterfs",
                      "QEMU #{v} still has the option"
    end

    refute_includes qemu.configure_flags(Ver("11.1.0")),
                    "--disable-glusterfs",
                    "11.1 removed it: passing it stops configure"
  end

  # The rest are options in every version this tree builds, and
  # dropping one silently would let a system library in.
  def test_the_other_options_are_passed_everywhere
    always = [
      "--enable-gtk", "--enable-vnc", "--disable-sdl", "--disable-curses",
      "--disable-libssh", "--disable-seccomp", "--disable-capstone",
      "--disable-docs",
    ]

    for v in HostQemuPackage::SUPPORTED do
      flags = qemu.configure_flags(v)
      for f in always do
        assert_includes flags, f, "#{f} missing for QEMU #{v}"
      end
    end
  end

  # Keyed by series, so a point release nobody listed still gets the
  # right compiler rather than falling back to HOST_VER_GCC.
  def test_an_unlisted_point_release_still_maps
    dep = qemu.dep_list_for(Ver("9.0.0")).find { |d|
      d.name == "host_gcc"
    }
    assert_equal HostQemuPackage::GCC_FOR[9], dep.ver
  end
end
