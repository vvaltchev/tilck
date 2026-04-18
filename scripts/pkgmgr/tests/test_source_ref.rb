# SPDX-License-Identifier: BSD-2-Clause
#
# Tests for SourceRef — the fetch/extract primitive consumed by
# Package. Covers default naming, per-kwarg overrides, and the
# git-vs-HTTP auto-detection heuristic.
#

require_relative 'test_helper'

class TestSourceRefDefaults < Minitest::Test

  def test_defaults
    src = SourceRef.new(name: 'foo', url: 'https://example.com/foo')
    v = Ver("1.2.3")
    assert_equal "foo-1.2.3.tgz", src.tarname(v)
    assert_equal "foo-1.2.3.tgz", src.remote_tarname(v)
    assert_equal "1.2.3",         src.git_tag(v)
    assert_equal "1.2.3",         src.dir_name(v)
  end

  def test_default_tarname_uses_name_not_version_object_class
    # Works whether ver is a Version or a String
    src = SourceRef.new(name: 'bar', url: 'https://example.com/bar')
    assert_equal "bar-abc.tgz", src.tarname("abc")
  end

  def test_attr_readers
    src = SourceRef.new(name: 'baz', url: 'https://example.com/baz')
    assert_equal 'baz',                   src.name
    assert_equal 'https://example.com/baz', src.url
  end
end

class TestSourceRefOverrides < Minitest::Test

  def test_tarname_override
    src = SourceRef.new(
      name: 'busybox',
      url:  'https://busybox.net/downloads',
      tarname: ->(ver) { "busybox-#{ver}.tar.bz2" },
    )
    assert_equal "busybox-1.36.1.tar.bz2", src.tarname(Ver("1.36.1"))
  end

  def test_git_tag_override_v_prefix
    src = SourceRef.new(
      name: 'libmusl',
      url:  'https://git.musl-libc.org/git/musl',
      git_tag: ->(ver) { "v#{ver}" },
    )
    assert_equal "v1.2.5", src.git_tag(Ver("1.2.5"))
  end

  def test_git_tag_override_constant
    # e.g. treecmd always checks out the "tilck" branch regardless of ver.
    src = SourceRef.new(
      name: 'treecmd',
      url:  'https://github.com/x/y',
      git_tag: ->(_ver) { "tilck" },
    )
    assert_equal "tilck", src.git_tag(Ver("1.8.0"))
    assert_equal "tilck", src.git_tag(Ver("99.0"))
  end

  def test_git_tag_can_return_nil
    # fbdoom pins no tag; the git fetcher then clones HEAD.
    src = SourceRef.new(
      name: 'fbdoom',
      url:  'https://github.com/maximevince/fbDOOM',
      git_tag: ->(_ver) { nil },
    )
    assert_nil src.git_tag(Ver("0.12.1"))
  end

  def test_remote_tarname_override
    # github /archive/ URLs serve the tarball as "<tag>.tar.gz" regardless
    # of the repo name.
    src = SourceRef.new(
      name: 'sophgo_host_tools',
      url:  'https://github.com/sophgo/host-tools/archive/refs/tags',
      tarname:        ->(ver) { "sophgo_host_tools-#{ver}.tar.gz" },
      remote_tarname: ->(ver) { "#{ver}.tar.gz" },
    )
    v = Ver("1.0.0")
    assert_equal "sophgo_host_tools-1.0.0.tar.gz", src.tarname(v)
    assert_equal "1.0.0.tar.gz",                   src.remote_tarname(v)
  end

  def test_remote_tarname_defaults_to_tarname
    src = SourceRef.new(
      name: 'foo',
      url:  'https://x',
      tarname: ->(ver) { "weird-#{ver}.tar.xz" },
    )
    v = Ver("1.0")
    assert_equal src.tarname(v), src.remote_tarname(v)
  end

  def test_dir_name_override
    src = SourceRef.new(
      name: 'foo',
      url:  'https://x',
      dir_name: ->(ver) { "custom-#{ver}" },
    )
    assert_equal "custom-1.0", src.dir_name(Ver("1.0"))
  end
end

class TestSourceRefFetchViaGit < Minitest::Test

  def test_github_repo_url_is_git
    src = SourceRef.new(name: 'x', url: 'https://github.com/u/r')
    assert src.fetch_via_git?
  end

  def test_github_releases_download_is_not_git
    src = SourceRef.new(
      name: 'x',
      url:  'https://github.com/u/r/releases/download/v1/asset.tar.gz',
    )
    refute src.fetch_via_git?
  end

  def test_github_archive_url_is_not_git
    src = SourceRef.new(
      name: 'x',
      url:  'https://github.com/u/r/archive/refs/tags',
    )
    refute src.fetch_via_git?
  end

  def test_non_github_url_is_not_git_by_default
    src = SourceRef.new(name: 'x', url: 'https://example.com/pkg.tar.gz')
    refute src.fetch_via_git?
  end

  def test_explicit_fetch_via_git_true_overrides_non_github
    # libmusl: non-github git server — must opt in explicitly.
    src = SourceRef.new(
      name: 'libmusl',
      url:  'https://git.musl-libc.org/git/musl',
      fetch_via_git: true,
    )
    assert src.fetch_via_git?
  end

  def test_explicit_fetch_via_git_false_overrides_github
    # An escape hatch for github URLs that should be fetched via HTTP
    # despite looking like a repo root.
    src = SourceRef.new(
      name: 'x',
      url:  'https://github.com/u/r',
      fetch_via_git: false,
    )
    refute src.fetch_via_git?
  end

  def test_explicit_fetch_via_git_true_does_not_break_github_heuristic
    # Redundant but valid: explicitly opting in for a github URL.
    src = SourceRef.new(
      name: 'x',
      url:  'https://github.com/u/r',
      fetch_via_git: true,
    )
    assert src.fetch_via_git?
  end
end

class TestSourceRefSharing < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_same_source_ref_can_be_attached_to_multiple_packages
    # A single SourceRef instance backing two Packages (gnuefi_src +
    # gnuefi is the real-world case). Both should expose the same
    # object identity for `source`, not a deep copy.
    shared = SourceRef.new(name: 'shared', url: 'https://ex/shared')
    a = FakePackage.new("a", source: shared)
    b = FakePackage.new("b", source: shared)
    assert_same shared, a.source
    assert_same shared, b.source
  end
end
