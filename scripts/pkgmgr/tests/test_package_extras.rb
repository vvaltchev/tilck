# SPDX-License-Identifier: BSD-2-Clause
#
# Tests for Package methods not covered by test_package.rb:
# apply_patches, fix_config_file, fetch_via_git?, configure, InstallInfo
#

require_relative 'test_helper'
require 'tmpdir'

class TestFetchViaGit < Minitest::Test
  include TestHelper

  def test_github_repo_url_is_git
    pkg = FakePackage.new("foo")
    pkg.define_singleton_method(:url) { "https://github.com/user/repo" }
    assert pkg.fetch_via_git?
  end

  def test_github_releases_url_is_not_git
    pkg = FakePackage.new("foo")
    pkg.define_singleton_method(:url) {
      "https://github.com/user/repo/releases/download/v1/file.tar.gz"
    }
    refute pkg.fetch_via_git?
  end

  def test_github_archive_url_is_not_git
    pkg = FakePackage.new("foo")
    pkg.define_singleton_method(:url) {
      "https://github.com/user/repo/archive/refs/tags"
    }
    refute pkg.fetch_via_git?
  end

  def test_non_github_url_is_not_git
    pkg = FakePackage.new("foo")
    pkg.define_singleton_method(:url) { "https://example.com/pkg.tar.gz" }
    refute pkg.fetch_via_git?
  end
end

class TestFixConfigFile < Minitest::Test
  include TestHelper

  def test_strips_header_and_normalizes
    Dir.mktmpdir do |dir|
      path = File.join(dir, ".config")

      # Write a fake kernel-style config with 4-line header
      File.write(path, <<~CONFIG)
        #
        # Automatically generated make config: don't edit
        # Linux/i386 5.4.0 Kernel Configuration
        # Mon Jan  1 00:00:00 UTC 2024
        CONFIG_FOO=y
        CONFIG_BAR=y

        # CONFIG_BAZ is not set
        some random line
      CONFIG

      pkg = FakePackage.new("foo")
      FileUtils.cd(dir) { pkg.fix_config_file(path) }

      result = File.read(path)
      lines = result.strip.split("\n")

      # Should only contain CONFIG_ lines
      lines.each { |l| assert_match(/CONFIG_/, l) }
      # Header should be gone
      refute_match(/Automatically generated/, result)
      # Random non-CONFIG lines should be gone
      refute_match(/some random line/, result)
      # Should be sorted in reverse binary order
      assert_equal lines, lines.sort { |a, b| -(a.b <=> b.b) }
    end
  end

  def test_empty_config
    Dir.mktmpdir do |dir|
      path = File.join(dir, ".config")
      File.write(path, "line1\nline2\nline3\nline4\n")

      pkg = FakePackage.new("foo")
      FileUtils.cd(dir) { pkg.fix_config_file(path) }

      result = File.read(path).strip
      assert_equal "", result
    end
  end
end

class TestApplyPatches < Minitest::Test
  include TestHelper

  def test_no_patch_dir_returns_true
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        # No patches directory exists → returns true (nothing to do = success)
        Dir.mktmpdir do |dir|
          FileUtils.cd(dir) { assert pkg.apply_patches(Ver("1.0.0")) }
        end
      end
    end
  end

  def test_empty_patch_dir_returns_true
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        # Create the patch directory but leave it empty
        patch_dir = MAIN_DIR / "scripts" / "patches" / "foo" / "1.0.0"
        FileUtils.mkdir_p(patch_dir)
        begin
          Dir.mktmpdir do |dir|
            FileUtils.cd(dir) { assert pkg.apply_patches(Ver("1.0.0")) }
          end
        ensure
          FileUtils.rm_rf(patch_dir)
        end
      end
    end
  end
end

class TestInstallInfo < Minitest::Test

  def test_to_s
    info = InstallInfo.new(
      "test_pkg", Ver("13.3.0"), false, ALL_ARCHS["i386"],
      Ver("1.0.0"), Pathname.new("/fake/path"), nil, false
    )
    s = info.to_s
    assert_match(/test_pkg/, s)
    assert_match(/1\.0\.0/, s)
    assert_match(/i386/, s)
  end

  def test_compiler_detection
    # Regular package — not a compiler
    info = InstallInfo.new(
      "foo", Ver("13.3.0"), false, ALL_ARCHS["i386"],
      Ver("1.0.0"), Pathname.new("/fake"), nil, false
    )
    refute info.compiler?

    # Compiler — has target_arch
    info = InstallInfo.new(
      "gcc-i386-musl", "syscc", true, HOST_ARCH,
      Ver("13.3.0"), Pathname.new("/fake"), nil, false,
      ALL_ARCHS["i386"], "musl"
    )
    assert info.compiler?
  end
end
