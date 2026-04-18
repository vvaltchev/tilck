# SPDX-License-Identifier: BSD-2-Clause
#
# Tests that capture design invariants after splitting the old
# `fbdoom` package (which bundled the game engine + freedoom WAD)
# into two independent packages:
#
#   - fbdoom:   the game engine, depends on freedoom.
#   - freedoom: the WAD data blob, independent upstream project.
#
# The split lets the dep graph express the relationship properly
# and keeps the two upstream artefacts cleanly separated.
#

require_relative 'test_helper'
require_relative '../fbdoom'
require_relative '../freedoom'

class TestFbDoomPackage < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    pkgmgr.register(FbDoomPackage.new)
    pkgmgr.register(FreedoomPackage.new)
  end

  def test_fbdoom_is_registered
    pkg = pkgmgr.get("fbdoom")
    refute_nil pkg
  end

  def test_freedoom_is_registered
    pkg = pkgmgr.get("freedoom")
    refute_nil pkg
  end

  def test_fbdoom_has_source
    pkg = pkgmgr.get("fbdoom")
    refute_nil pkg.source
    assert_kind_of SourceRef, pkg.source
  end

  def test_freedoom_has_source
    pkg = pkgmgr.get("freedoom")
    refute_nil pkg.source
    assert_kind_of SourceRef, pkg.source
  end

  def test_fbdoom_and_freedoom_do_not_share_sourceref
    # They're separate upstream projects; a shared SourceRef would
    # incorrectly imply one tarball satisfies both.
    fb = pkgmgr.get("fbdoom")
    fr = pkgmgr.get("freedoom")
    refute_same fb.source, fr.source
  end

  def test_fbdoom_depends_on_freedoom
    # The engine needs the WAD to run; encode that at the pkg level.
    pkg = pkgmgr.get("fbdoom")
    dep_names = pkg.dep_list.map(&:name)
    assert_includes dep_names, "freedoom"
  end

  def test_freedoom_does_not_depend_on_fbdoom
    # The WAD is usable by other engines — no reason to pull in
    # fbdoom when a user explicitly installs just freedoom.
    pkg = pkgmgr.get("freedoom")
    dep_names = pkg.dep_list.map(&:name)
    refute_includes dep_names, "fbdoom"
  end

  def test_fbdoom_is_x86_family
    # x86 only: Tilck runs fbDOOM on i386, and x86_64 is kept
    # installable so the cross-compile path is exercised.
    pkg = pkgmgr.get("fbdoom")
    assert_equal ["i386", "x86_64"].sort,
                 pkg.arch_list.map(&:name).sort
  end

  def test_freedoom_is_x86_family
    pkg = pkgmgr.get("freedoom")
    assert_equal ["i386", "x86_64"].sort,
                 pkg.arch_list.map(&:name).sort
  end

  def test_fbdoom_expected_files
    # Post-split the engine install dir contains only fbdoom.gz —
    # the WAD lives in the freedoom package.
    pkg = pkgmgr.get("fbdoom")
    files = pkg.expected_files
    assert_includes files, ["fbdoom.gz", false]
    # Freedoom-related paths must NOT appear under the fbdoom dir.
    refute(files.any? { |entry, _| entry.include?("freedoom") })
  end

  def test_freedoom_expected_files
    pkg = pkgmgr.get("freedoom")
    files = pkg.expected_files
    assert_includes files, ["freedoom1.wad.gz", false]
  end

  def test_fbdoom_source_uses_github_git_clone
    src = pkgmgr.get("fbdoom").source
    assert src.fetch_via_git?
  end

  def test_fbdoom_source_pins_no_tag
    # Upstream maximevince/fbDOOM has no releases — the install
    # always clones HEAD. git_tag(ver) must return nil to tell the
    # cache/git layer to shallow-clone HEAD.
    src = pkgmgr.get("fbdoom").source
    assert_nil src.git_tag(Ver("0.12.1"))
  end

  def test_freedoom_cache_filename_is_zip
    src = pkgmgr.get("freedoom").source
    assert_equal "freedoom-0.12.1.zip", src.tarname(Ver("0.12.1"))
  end
end
