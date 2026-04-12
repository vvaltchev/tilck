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
      arch_dir = tc / "gcc-#{gcc}" / ARCH.name
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

  def test_missing_expected_file
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) {
        [["required_binary", false]]
      }
      refute pkg.check_install_dir(d)
    end
  end

  def test_missing_expected_directory
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) {
        [["required_dir", true]]
      }
      refute pkg.check_install_dir(d)
    end
  end

  def test_missing_file_with_report_error
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) {
        [["missing", false]]
      }
      refute pkg.check_install_dir(d, true)
    end
  end

  def test_missing_dir_with_report_error
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) {
        [["missing_dir", true]]
      }
      refute pkg.check_install_dir(d, true)
    end
  end

  def test_all_present_passes
    Dir.mktmpdir do |dir|
      d = Pathname.new(dir)
      FileUtils.touch(d / "binary")
      FileUtils.mkdir_p(d / "subdir")
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:expected_files) {
        [["binary", false], ["subdir", true]]
      }
      assert pkg.check_install_dir(d)
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

class TestInstallImplNoUrl < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_raises_when_url_nil
    with_fake_tc do |tc|
      pkg = FakePackage.new("foo")
      pkg.define_singleton_method(:url) { nil }
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
end
