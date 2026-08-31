# SPDX-License-Identifier: BSD-2-Clause
#
# Tests targeting specific uncovered lines in package.rb.
#

require_relative 'test_helper'
require 'tmpdir'

class TestChdirInstallDirMissing < Minitest::Test
  include TestHelper

  def test_returns_false_when_dir_missing
    with_fake_tc do |tc|
      pkg = FakePackage.new("foo")
      gcc = FAKE_GCC_VER.to_s
      arch_dir = target_pkgs(ARCH, gcc)
      # Don't create foo/1.0.0 — it doesn't exist
      result = pkg.chdir_install_dir(arch_dir, Ver("1.0.0")) { true }
      assert_equal false, result
    end
  end
end

class TestApplyPatchesCoverage < Minitest::Test
  include TestHelper

  def test_applies_common_patch
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")

        # Create the patch directory with a real diff
        patch_dir = MAIN_DIR / "scripts" / "patches" / "foo" / "1.0.0"
        FileUtils.mkdir_p(patch_dir)

        # Create a simple patch file
        File.write(patch_dir / "001-test.diff",
          "--- /dev/null\n+++ b/patched.txt\n@@ -0,0 +1 @@\n+patched\n")

        begin
          Dir.mktmpdir do |workdir|
            FileUtils.cd(workdir) do
              # stub system("patch", ...) to succeed
              pkg.define_singleton_method(:system) { |*args| true }
              result = pkg.apply_patches(Ver("1.0.0"))
              assert_equal true, result
            end
          end
        ensure
          FileUtils.rm_rf(patch_dir)
        end
      end
    end
  end

  def test_applies_arch_specific_patch
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")

        patch_dir = MAIN_DIR / "scripts" / "patches" / "foo" / "1.0.0"
        arch_patch_dir = patch_dir / ARCH.name
        FileUtils.mkdir_p(arch_patch_dir)

        File.write(arch_patch_dir / "001-arch.diff",
          "--- /dev/null\n+++ b/arch_patched.txt\n@@ -0,0 +1 @@\n+arch\n")

        begin
          Dir.mktmpdir do |workdir|
            FileUtils.cd(workdir) do
              pkg.define_singleton_method(:system) { |*args| true }
              result = pkg.apply_patches(Ver("1.0.0"))
              assert_equal true, result
            end
          end
        ensure
          FileUtils.rm_rf(patch_dir)
        end
      end
    end
  end

  def test_patch_failure_returns_false
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")

        patch_dir = MAIN_DIR / "scripts" / "patches" / "foo" / "1.0.0"
        FileUtils.mkdir_p(patch_dir)
        File.write(patch_dir / "001-bad.diff", "garbage patch")

        begin
          Dir.mktmpdir do |workdir|
            FileUtils.cd(workdir) do
              # stub system("patch", ...) to FAIL
              pkg.define_singleton_method(:system) { |*args| false }
              result = pkg.apply_patches(Ver("1.0.0"))
              assert_equal false, result
            end
          end
        ensure
          FileUtils.rm_rf(patch_dir)
        end
      end
    end
  end
end

class TestCheckInstallDirCoverage < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_missing_expected_file
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) { |ver = nil|
        [["required_binary", false]]
      }
      refute pkg.check_install_dir(d, Ver("1.0.0"))
    end
  end

  def test_missing_expected_directory
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) { |ver = nil|
        [["required_dir", true]]
      }
      refute pkg.check_install_dir(d, Ver("1.0.0"))
    end
  end

  def test_missing_file_with_report_error
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) { |ver = nil|
        [["missing", false]]
      }
      refute pkg.check_install_dir(d, Ver("1.0.0"), true)
    end
  end

  def test_missing_dir_with_report_error
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) { |ver = nil|
        [["missing_dir", true]]
      }
      refute pkg.check_install_dir(d, Ver("1.0.0"), true)
    end
  end

  def test_all_present_passes
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      FileUtils.touch(d / "binary")
      FileUtils.mkdir_p(d / "subdir")
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) { |ver = nil|
        [["binary", false], ["subdir", true]]
      }
      assert pkg.check_install_dir(d, Ver("1.0.0"))
    end
  end

  # expected_files takes the version so a package whose install layout
  # diverged across versions can return a different list. Most packages
  # ignore the argument; these two check the plumbing really delivers it.
  def test_expected_files_receives_the_version
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      FileUtils.touch(d / "old_binary")

      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) { |ver = nil|
        ver >= Ver("2.0.0") ? [["new_binary", false]] : [["old_binary", false]]
      }

      assert pkg.check_install_dir(d, Ver("1.0.0"))
      refute pkg.check_install_dir(d, Ver("2.0.0"))

      FileUtils.touch(d / "new_binary")
      assert pkg.check_install_dir(d, Ver("2.0.0"))
    end
  end

  # The install-list scanners must hand each entry ITS OWN version, not
  # just any version: 1.0.0 stays fine while 2.0.0 is flagged broken.
  def test_scanner_uses_each_versions_own_expected_files
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))
        pkg.install_impl(Ver("2.0.0"))

        # Demand a file only from 2.0.0; neither install has it.
        pkg.define_singleton_method(:expected_files) { |ver = nil|
          ver >= Ver("2.0.0") ? [["missing", false]] : []
        }

        list = pkg.get_install_list
        refute list.find { |x| x.ver == Ver("1.0.0") }.broken
        assert list.find { |x| x.ver == Ver("2.0.0") }.broken
      end
    end
  end
end

class TestInstallImplGitPath < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_fetch_via_git_path
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkg.define_singleton_method(:fetch_via_git?) { true }
        pkgmgr.register(pkg)
        result = pkgmgr.install("foo")
        assert result
      end
    end
  end
end

class TestInstallImplNoSource < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_raises_when_source_nil
    # A package with source: nil and no custom install_impl falls
    # through to the base flow, which has no way to fetch anything
    # and must raise NotImplementedError.
    with_fake_tc do |tc|
      pkg = FakePackage.new("foo", source: nil)
      pkgmgr.register(pkg)
      assert_raises(NotImplementedError) {
        pkg.install_impl(Ver("1.0.0"))
      }
    end
  end
end

class TestConfigureCoverage < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_configure_not_installed
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkg.define_singleton_method(:configurable?) { true }
        pkgmgr.register(pkg)
        result = pkg.configure
        assert_equal false, result
      end
    end
  end

  def test_configure_success
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkg.define_singleton_method(:configurable?) { true }
        pkg.define_singleton_method(:config_impl) { true }
        pkgmgr.register(pkg)
        pkgmgr.install("foo")

        result = pkg.configure
        assert_equal true, result
      end
    end
  end

  # `-C pkg:ver` reaches configure(ver): with several versions on disk
  # the requested one is the one entered, and asking for an
  # uninstalled version fails rather than silently picking another.
  def test_configure_enters_the_requested_version
    with_fake_tc do
      with_stubbed_externals do
        seen = []
        pkg = FakePackage.new("foo")
        pkg.define_singleton_method(:configurable?) { true }
        pkg.define_singleton_method(:config_impl) {
          seen << Dir.pwd
          true
        }
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))
        pkg.install_impl(Ver("2.0.0"))

        assert_equal true, pkg.configure(Ver("2.0.0"))
        assert_equal 1, seen.length
        assert_match(%r{/foo/2\.0\.0\z}, seen.first)

        assert_equal true, pkg.configure(Ver("1.0.0"))
        assert_match(%r{/foo/1\.0\.0\z}, seen.last)
      end
    end
  end

  def test_configure_rejects_an_uninstalled_version
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkg.define_singleton_method(:configurable?) { true }
        pkg.define_singleton_method(:config_impl) { true }
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))

        assert_equal false, pkg.configure(Ver("9.9.9"))
      end
    end
  end
end
