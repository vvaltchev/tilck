# SPDX-License-Identifier: BSD-2-Clause
#
# The real stack package definitions: tiers, dependencies, gating
# and the build-order invariants that keep the bootstrap possible.
#

require_relative 'test_helper'
require_relative '../binutils'
require_relative '../linux_headers'
require_relative '../glibc'

class TestPortablePackageDefs < Minitest::Test
  include TestHelper

  # Register fresh instances rather than relying on the ones created at
  # file load: another test class's reset_pkgmgr! may have cleared the
  # registry before this one runs.
  def setup
    reset_pkgmgr!
    pkgmgr.register(HostBinutilsPackage.new)
    pkgmgr.register(HostLinuxHeadersPackage.new)
    pkgmgr.register(HostGlibcPackage.new)
  end

  def pkg(name) = pkgmgr.get(name)

  def test_all_three_are_registered
    for n in ["host_binutils", "host_linux_headers", "host_glibc"]
      refute_nil pkg(n), "#{n} is not registered"
    end
  end

  # The tier describes what a package's OWN binaries depend on.
  # binutils is built by the system compiler against the system libc,
  # so it is an ordinary distro package; the headers and libc it helps
  # produce are what belong to the composed sysroot.
  def test_binutils_is_a_distro_package
    assert_equal :distro, pkg("host_binutils").host_tier
  end

  def test_sysroot_contents_are_portable
    assert_equal :stack, pkg("host_linux_headers").host_tier
    assert_equal :stack, pkg("host_glibc").host_tier
  end

  # All of it is opt-in: building a libc is not something to stumble
  # into.
  def test_none_of_it_is_in_the_default_set
    for n in ["host_binutils", "host_linux_headers", "host_glibc"]
      refute pkg(n).default?, "#{n} must not be a default package"
    end
  end

  def test_gating_follows_the_environment_switch
    for n in ["host_binutils", "host_linux_headers", "host_glibc"]
      assert_equal HOST_STACK_ENABLED, pkg(n).enabled?, n
    end
  end

  # glibc compiles against the kernel headers, so the headers must be
  # installed first. Nothing else in the bootstrap has a dependency:
  # that is the whole point of building for the host's own triple.
  def test_glibc_depends_on_the_kernel_headers
    deps = pkg("host_glibc").dep_list.map(&:name)
    assert_includes deps, "host_linux_headers"
  end

  def test_headers_and_binutils_depend_on_nothing
    assert_empty pkg("host_linux_headers").dep_list
    assert_empty pkg("host_binutils").dep_list
  end

  def test_the_bootstrap_graph_has_no_cycle
    DepResolver.validate_no_cycles(pkgmgr.build_dep_graph)
  end

  # Every one of them must resolve to a version, or the install would
  # go to a truncated path.
  def test_all_have_versions
    for n in ["host_binutils", "host_linux_headers", "host_glibc"]
      refute_nil pkg(n).default_ver, "#{n} has no version"
    end
  end

  # The floor that forced glibc 2.41 rather than 2.42: the first glibc
  # has to be buildable by the oldest supported host compiler, because
  # there is no compiler of ours yet. See the comment in glibc.rb.
  def test_glibc_is_within_the_bootstrappable_range
    assert pkg("host_glibc").default_ver <= Ver("2.41"),
           "glibc > 2.41 requires GCC 12.1+, which the oldest supported " \
           "build host (Ubuntu 22.04, GCC 11.4) does not have"
  end

  def test_glibc_minimum_kernel_is_declared
    assert_equal "4.19", GLIBC_MIN_KERNEL
  end
end
