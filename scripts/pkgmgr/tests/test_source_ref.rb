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

class TestSourceRefPins < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_the_files_a_source_puts_in_the_cache_and_their_pins
    src = SourceRef.new(name: 'py', url: 'https://ex/py',
                        extra_files: [{ url: 'https://ex/w',
                                        file: 'w-1.whl' }])
    assert_equal ['py-1.0.tgz', 'w-1.whl'], src.cache_files(Ver('1.0'))
    held = pkgmgr.pins
    pkgmgr.pins = { 'w-1.whl' => pin_of("w") }
    assert_nil src.pin('py-1.0.tgz')
    assert_equal pin_of("w"), src.pin('w-1.whl')
  ensure
    pkgmgr.pins = held
  end

  def test_a_packed_source_is_named_with_the_packer_s_extension
    src = SourceRef.new(name: 'zlib', url: GITHUB + '/madler/zlib')
    assert src.fetch_via_git?
    assert_equal "zlib-v1.2#{Cache::Pack::EXT}", src.tarname("v1.2")
  end

  def test_download_and_extract_hand_the_cache_each_file_s_pin
    src = SourceRef.new(name: 'py', url: 'https://ex/py',
                        fetch_via_git: false,
                        extra_files: [{ url: 'https://ex/w',
                                        file: 'w-1.whl' }])
    seen = []
    held = pkgmgr.pins
    pkgmgr.pins = { 'py-1.0.tgz' => pin_of("t"), 'w-1.whl' => pin_of("w") }
    with_fake_tc do
      with_stubbed_externals do
        Cache.define_singleton_method(:download_file) {
          |url, remote, local = nil, pin:|
          seen << [:dl, local || remote, pin]
          true
        }
        Cache.define_singleton_method(:extract_file) {
          |tarfile, dest = nil, pin:|
          seen << [:x, tarfile, pin]
          true
        }
        assert src.download(Ver('1.0'))
        assert src.extract(Ver('1.0'), 'dest')
      end
    end
    assert_equal [[:dl, 'py-1.0.tgz', pin_of("t")],
                  [:dl, 'w-1.whl', pin_of("w")],
                  [:x, 'py-1.0.tgz', pin_of("t")]], seen
  ensure
    pkgmgr.pins = held
  end
end

#
# A package with more than one source: what upstream keeps as git
# submodules, declared as subsources (Package#subsources) and placed
# in the tree before the recipe runs.
#
class TestSubsources < Minitest::Test
  include TestHelper

  SUB = SourceRef.new(name: 'sub', url: GITHUB + '/x/sub',
                      git_tag: ->(v) { "c" * 40 })

  class WithSub < TestHelper::FakePackage
    def subsources(ver = default_ver)
      [Subsource.new(source: SUB, ver: ver, into: "lib/sub")]
    end
  end

  def setup
    reset_pkgmgr!
    @held = pkgmgr.pins
  end

  def teardown
    pkgmgr.pins = @held
  end

  def test_a_package_reads_its_own_source_and_its_subsources
    pkg = WithSub.new("main")
    assert_equal [[pkg.source, Ver("1.0.0")], [SUB, Ver("1.0.0")]],
                 pkg.sources_at(Ver("1.0.0"))
    assert_equal [], FakePackage.new("plain").subsources
  end

  def test_a_build_fetches_and_places_each_subsource_and_records_it
    pkg = WithSub.new("main")
    pkgmgr.register(pkg)
    pin = SourcePins.commit("c" * 40)
    pkgmgr.pins = { "main-1.0.0.tgz" => pin_of("main"), "sub-1.0.0.tgz" => pin }
    fetched = []
    placed = []
    with_fake_tc do
      with_stubbed_externals do
        Cache.define_singleton_method(:download_git_repo) {
          |url, tarname, tag = nil, dir_name = nil, pin: nil|
          fetched << [tarname, tag, pin]
          FileUtils.touch(TC_CACHE / tarname)
          true
        }
        Cache.define_singleton_method(:extract_file) {
          |tarfile, dest = nil, pin: nil|
          placed << [tarfile, dest, Pathname.pwd.basename.to_s]
          FileUtils.mkdir_p(dest)
          true
        }
        assert pkgmgr.install("main")
        inst = bound(pkg).find_install(Ver("1.0.0"))
        assert_equal({ "main-1.0.0.tgz" => pin_of("main").to_s,
                       "sub-1.0.0.tgz" => pin.to_s },
                     BuildInputs.sources_of(inst.path))
      end
    end
    # The fake's own source is a download (stubbed with the rest);
    # the subsource is a clone, at its commit, with its pin.
    assert_equal [["sub-1.0.0.tgz", "c" * 40, pin]], fetched
    # The subsource is extracted inside the extracted tree.
    assert_equal [["main-1.0.0.tgz", "1.0.0", "main"],
                  ["sub-1.0.0.tgz", "lib/sub", "1.0.0"]], placed
  end

  def test_the_registry_lists_a_subsource_s_file_with_its_source
    pkg = WithSub.new("main")
    pkgmgr.register(pkg)
    with_fake_tc do
      files = pkgmgr.source_files
      assert_equal [["main", Ver("1.0.0"), SUB]], files["sub-1.0.0.tgz"]
      assert_equal [["main", Ver("1.0.0"), pkg.source]], files["main-1.0.0.tgz"]
    end
  end

  def test_an_archive_replaces_the_empty_placeholder_a_tree_keeps_for_it
    with_fake_tc do |tc|
      Dir.mktmpdir do |staging|
        FileUtils.mkdir_p(File.join(staging, "v1"))
        File.write(File.join(staging, "v1", "f.c"), "code")
        system("tar", "cfz", (tc / "cache" / "sub-1.tgz").to_s,
               "-C", staging, "v1")
      end
      pin = SourcePins.digest_of(tc / "cache" / "sub-1.tgz")
      tree = noarch_pkgs / "main" / "1.0.0"
      FileUtils.mkdir_p(tree / "lib" / "sub")   # the placeholder
      FileUtils.cd(tree) do
        assert Cache.extract_file("sub-1.tgz", "lib/sub", pin: pin)
      end
      assert (tree / "lib" / "sub" / "f.c").file?
      refute (tree / "lib" / "sub" / "v1").exist?
    end
  end
end
