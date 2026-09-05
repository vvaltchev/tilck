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

  # Keyed by series, so a point release nobody listed still gets the
  # right compiler rather than falling back to HOST_VER_GCC.
  def test_an_unlisted_point_release_still_maps
    dep = qemu.dep_list_for(Ver("9.0.0")).find { |d|
      d.name == "host_gcc"
    }
    assert_equal HostQemuPackage::GCC_FOR[9], dep.ver
  end
end
