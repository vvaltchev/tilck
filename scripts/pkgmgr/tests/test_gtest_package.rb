# SPDX-License-Identifier: BSD-2-Clause
#
# Tests that capture design invariants for the gtest package after
# the "source as a SourceRef, not a Package" refactor:
#
#   - gtest_src is no longer a registered Package.
#   - host_gtest owns GTEST_SOURCE directly (no gtest_src dep).
#   - The SourceRef carries the "v" tag prefix and the git-clone
#     fetch strategy that was previously a method override on
#     GtestSrcPackage.
#

require_relative 'test_helper'
require_relative '../gtest'

class TestGtestNoSourcePackage < Minitest::Test
  include TestHelper

  def setup
    # Start from a clean registry, then register only the gtest
    # package under test. This is robust against test-order shuffling
    # — some earlier test may have wiped the registry via
    # reset_pkgmgr!.
    reset_pkgmgr!
    pkgmgr.register(GtestPackage.new)
  end

  def test_gtest_src_is_not_registered
    # gtest_src is an implementation detail that used to leak as a
    # user-visible package. After the refactor it should be invisible
    # to `-l`, `-s`, dep resolution, everything.
    assert_nil pkgmgr.get("gtest_src")
  end

  def test_host_gtest_is_registered
    pkg = pkgmgr.get("host_gtest")
    refute_nil pkg
    assert_equal "host_gtest", pkg.name
  end

  def test_host_gtest_has_source
    pkg = pkgmgr.get("host_gtest")
    refute_nil pkg.source
    assert_kind_of SourceRef, pkg.source
  end

  def test_host_gtest_has_no_gtest_src_dep
    # The gtest_src dep used to be the thing providing the source;
    # after the refactor host_gtest owns its SourceRef directly, so
    # the dep must disappear.
    pkg = pkgmgr.get("host_gtest")
    dep_names = pkg.dep_list.map(&:name)
    refute_includes dep_names, "gtest_src"
  end

  def test_host_gtest_source_uses_v_prefix_tag
    # Upstream google/googletest tags are prefixed with "v" — this
    # was a git_tag method override on GtestSrcPackage, now it lives
    # on the SourceRef.
    src = pkgmgr.get("host_gtest").source
    assert_equal "v1.17.0", src.git_tag(Ver("1.17.0"))
  end

  def test_host_gtest_source_uses_github_git_clone
    # Default SourceRef heuristic: github.com repo URL -> git clone.
    src = pkgmgr.get("host_gtest").source
    assert src.fetch_via_git?
  end
end
