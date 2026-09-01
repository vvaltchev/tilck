# SPDX-License-Identifier: BSD-2-Clause
#
# A patch directory belongs to exactly one package.
#
# It used to be keyed on pkg_dirname, which names a SOURCE DIRECTORY
# and is shared on purpose: host_ncurses and ncurses build the same
# sources for different machines, host_zlib and zlib likewise, and
# gnuefi_src hands out the very headers gnuefi compiles. Their install
# directories stay distinct because the coordinates disambiguate them.
# A patch path has no coordinates in it, so those three pairs shared
# one directory -- and a patch dropped in it was applied to both
# packages and recorded among both packages' build inputs, silently.
#
# Two of them collided at the same version, so it was not theoretical:
# it is what stopped gnuefi and ncurses from being converted to
# patches at all.
#

require_relative 'test_helper'
require_relative '../main'      # registers every package

class TestPatchDirectoriesAreNotShared < Minitest::Test

  include TestHelper

  # Snapshotted at LOAD time, deliberately. This asks about the real
  # package set, and reset_pkgmgr! empties the registry for whichever
  # test needs a clean one -- with a randomised order, reading the
  # registry from inside a test is reading whatever the previous test
  # left. Package objects keep answering after a reset; the registry
  # does not.
  PACKAGES = pkgmgr.all_packages.dup.freeze

  def all_pkgs = PACKAGES
  def pkg(name) = PACKAGES.find { |p| p.name == name }

  def test_no_two_packages_share_a_patch_directory
    by_dir = Hash.new { |h, k| h[k] = [] }
    all_pkgs.each { |p| by_dir[p.patch_dirname] << p.name }

    shared = by_dir.select { |_, names| names.length > 1 }
    assert_empty shared,
                 "these packages would receive each other's patches: " +
                 shared.map { |d, n| "#{d} <- #{n.join(', ')}" }.join("; ")
  end

  # The pairs that made this necessary, named so that a regression
  # says which relationship broke rather than just "two things clash".
  def test_the_pairs_that_share_a_source_directory_stay_apart
    pairs = [
      ["ncurses", "host_ncurses"],
      ["zlib", "host_zlib"],
      ["gnuefi", "gnuefi_src"],
    ]

    for a, b in pairs do
      pa, pb = pkg(a), pkg(b)
      refute_nil pa, "package #{a} is gone"
      refute_nil pb, "package #{b} is gone"
      assert_equal pa.pkg_dirname, pb.pkg_dirname,
                   "#{a}/#{b} no longer share a source directory: " \
                   "this test is guarding something that moved"
      refute_equal pa.patch_dirname, pb.patch_dirname,
                   "#{a} and #{b} would share a patch directory"
    end
  end

  # The prefixes, stated directly: a reader looking at
  # scripts/patches/ should be able to tell what a directory is for.
  def test_the_prefix_says_which_machine_the_package_is_for
    for p in all_pkgs do
      d = p.patch_dirname
      if p.on_host
        assert d.start_with?("host_"), "#{p.name}: host package as #{d}"
      elsif p.arch_list.nil?
        refute d.start_with?("host_", "target_"), "#{p.name}: noarch as #{d}"
      else
        assert d.start_with?("target_"), "#{p.name}: target package as #{d}"
      end
    end
  end

  # Every checked-in patch directory belongs to a package. A leftover
  # one is not harmless: it is applied to whatever package later takes
  # that name.
  def test_every_patch_directory_has_an_owner
    root = MAIN_DIR / "scripts" / "patches"
    return if !root.directory?

    owned = all_pkgs.map(&:patch_dirname).to_set
    found = Dir.children(root).select { |d| (root / d).directory? }.sort

    assert_empty found - owned.to_a,
                 "patch directories owned by no package"
  end
end
