# SPDX-License-Identifier: BSD-2-Clause
#
# HostGccPackage::PREREQS must say what GCC says.
#
# The table exists because dependency resolution happens before any
# source is fetched: nothing can read the versions out of a tarball at
# the moment they are needed. That makes it a second copy of a fact
# upstream already states, in contrib/download_prerequisites, and a
# second copy is a thing that goes quietly wrong -- a point release
# bumps mpfr, the table does not, and a compiler gets built against a
# library its own sources were never tested with. It would build.
#
# So check it. Every GCC tarball we have cached is opened and its own
# script read; a version we have not cached is reported as unchecked
# rather than passed over in silence.
#

require_relative 'test_helper'
require_relative '../main'

class TestGccPrereqsMatchUpstream < Minitest::Test

  include TestHelper

  PKG = HostGccPackage

  # Snapshotted at LOAD time. reset_pkgmgr! empties the registry for
  # whichever test wants a clean one, and with a randomised order,
  # reading it from inside a test is reading whatever ran before.
  # Package objects keep answering after a reset; the registry does
  # not. (Same reason as test_patch_dirs.rb.)
  PACKAGES = pkgmgr.all_packages.dup.freeze

  def pkg(name) = PACKAGES.find { |p| p.name == name }

  def tarball_for(ver)
    f = TC_CACHE / "gcc-#{ver}.tar.xz"
    return f.file? ? f : nil
  end

  # The four names and the base URL, out of the script inside the
  # tarball -- the same file the build used to run.
  def upstream_prereqs(tar)
    out = `tar -xOf #{tar} --wildcards '*/contrib/download_prerequisites' 2>/dev/null`
    return nil if out.empty?

    names = out.scan(/^\s*(gmp|mpfr|mpc|isl)=['"]([^'"]+)['"]/)
    return nil if names.length < 4

    return names.to_h { |k, file|
      [k.to_sym, file[/#{k}-([0-9.]+)\.tar/, 1]]
    }
  end

  def test_every_supported_version_has_a_prereq_entry
    missing = PKG::SUPPORTED.reject { |v| PKG::PREREQS.key?(v) }
    assert_empty missing.map(&:to_s),
                 "supported GCC versions with no prerequisites declared"
  end

  def test_the_table_matches_what_each_gcc_asks_for
    cached = PKG::SUPPORTED.select { |v| tarball_for(v) }

    # Nothing to compare against. A CI container starts with an empty
    # cache and never downloads a host GCC at all -- the unit tests
    # are the first step, and the toolchain it goes on to build uses
    # the prebuilt cross compilers. Saying "skipped" is true there;
    # failing would assert the table is wrong, which is a different
    # claim and not one this test has evidence for. Which versions
    # went unchecked is named by the test below.
    skip "no cached GCC tarball to check the table against" if cached.empty?

    for ver in cached do
      tar = tarball_for(ver)
      up = upstream_prereqs(tar)
      refute_nil up, "cannot read download_prerequisites from #{tar}"

      ours = PKG::PREREQS[ver]
      for k in [:gmp, :mpfr, :mpc, :isl] do
        assert_equal up[k], ours[k],
                     "gcc #{ver}: #{k} is #{ours[k]} in PREREQS but " \
                     "#{up[k]} in contrib/download_prerequisites"
      end
    end
  end

  # Says what it could not check, rather than passing quietly.
  def test_report_versions_that_could_not_be_checked
    unchecked = PKG::SUPPORTED.reject { |v| tarball_for(v) }
    return if unchecked.empty?

    skip "not cached, so unchecked against upstream: " +
         unchecked.map(&:to_s).join(", ")
  end

  # The packages the table names have to exist, or the pins point at
  # nothing.
  def test_every_pinned_package_is_registered
    for n in PKG::PREREQ_NAMES do
      refute_nil pkg(n), "#{n} is pinned but not registered"
    end
  end

  # Each GCC's dep list carries its own versions.
  def test_each_gcc_pins_its_own_versions
    g = pkg("host_gcc")

    v11 = g.dep_list_for(Ver("11.5.0")).to_h { |d| [d.name, d.ver.to_s] }
    v16 = g.dep_list_for(Ver("16.2.0")).to_h { |d| [d.name, d.ver.to_s] }

    assert_equal "6.1.0", v11["host_gmp"]
    assert_equal "6.3.0", v16["host_gmp"]
    refute_equal v11["host_mpfr"], v16["host_mpfr"]
  end
end
