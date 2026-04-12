# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

# ---------------------------------------------------------------
# Tests for the pure-logic methods: these don't need filesystem
# or stubbed externals — they just check attribute-based decisions.
# ---------------------------------------------------------------

class TestPackageHostSupported < Minitest::Test
  include TestHelper

  def test_no_constraints
    pkg = FakePackage.new("foo")
    assert pkg.host_supported?
  end

  def test_matching_os
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: [HOST_OS])
    assert pkg.host_supported?
  end

  def test_wrong_os
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: ["nope_os"])
    refute pkg.host_supported?
  end

  def test_matching_arch
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_arch_list: [HOST_ARCH.name])
    assert pkg.host_supported?
  end

  def test_wrong_arch
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_arch_list: ["nope_arch"])
    refute pkg.host_supported?
  end

  def test_both_constraints_match
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: [HOST_OS],
                          host_arch_list: [HOST_ARCH.name])
    assert pkg.host_supported?
  end
end

class TestPackageBoardSupported < Minitest::Test
  include TestHelper

  def test_nil_means_any
    pkg = FakePackage.new("foo")
    assert pkg.board_supported?
  end

  def test_matching_board
    with_context(BOARD: "test-board") do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      assert pkg.board_supported?
    end
  end

  def test_wrong_board
    with_context(BOARD: "other-board") do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      refute pkg.board_supported?
    end
  end

  def test_nil_board_with_board_list
    with_context(BOARD: nil) do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      refute pkg.board_supported?
    end
  end
end

class TestPackageArchSupported < Minitest::Test
  include TestHelper

  def test_all_archs
    pkg = FakePackage.new("foo", arch_list: ALL_ARCHS)
    assert pkg.arch_supported?
  end

  def test_nil_means_noarch
    pkg = FakePackage.new("foo", arch_list: nil)
    assert pkg.arch_supported?
  end

  def test_host_package_always_true
    pkg = FakePackage.new("host_foo", on_host: true,
                          arch_list: ALL_HOST_ARCHS)
    assert pkg.arch_supported?
  end

  def test_wrong_arch
    with_context(ARCH: ALL_ARCHS["i386"]) do
      pkg = FakePackage.new("foo",
                            arch_list: { "riscv64" => ALL_ARCHS["riscv64"] })
      refute pkg.arch_supported?
    end
  end

  def test_matching_arch
    with_context(ARCH: ALL_ARCHS["i386"]) do
      pkg = FakePackage.new("foo",
                            arch_list: { "i386" => ALL_ARCHS["i386"] })
      assert pkg.arch_supported?
    end
  end
end

class TestPackageDefault < Minitest::Test
  include TestHelper

  def test_false_by_default
    refute FakePackage.new("foo").default?
  end

  def test_true_when_set
    assert FakePackage.new("foo", default: true).default?
  end

  def test_gated_by_arch
    with_context(ARCH: ALL_ARCHS["i386"]) do
      pkg = FakePackage.new("foo", default: true,
                            arch_list: { "riscv64" => ALL_ARCHS["riscv64"] })
      refute pkg.default?
    end
  end

  def test_gated_by_host_os
    pkg = FakePackage.new("host_foo", on_host: true, default: true,
                          host_os_list: ["nope_os"])
    refute pkg.default?
  end

  def test_gated_by_board
    with_context(BOARD: "other-board") do
      pkg = FakePackage.new("foo", default: true,
                            board_list: ["test-board"])
      refute pkg.default?
    end
  end

  def test_passes_all_gates
    pkg = FakePackage.new("foo", default: true,
                          arch_list: { ARCH.name => ARCH })
    assert pkg.default?
  end
end

# ---------------------------------------------------------------
# Tests that exercise real Package code paths with a fake TC
# directory and stubbed externals.
# ---------------------------------------------------------------

class TestPackageInstallReal < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_install_creates_dir_and_records
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        result = pkgmgr.install("foo")
        assert result
        assert_equal ["foo"], FakePackage.install_log
        assert pkg.installed?(Ver("1.0.0"))
      end
    end
  end

  def test_install_already_installed_skips
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)

        # First install
        pkgmgr.install("foo")
        FakePackage.clear_log!

        # Second install — should skip
        result = pkgmgr.install("foo")
        assert result  # nil (skip) is truthy via the nil? ternary
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_install_host_not_supported_fails_early
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true,
                              host_os_list: ["nope_os"])
        pkgmgr.register(pkg)
        result = pkg.install_impl(Ver("1.0.0"))
        assert_equal false, result
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_install_board_not_supported_fails_early
    with_fake_tc do |tc|
      with_context(BOARD: "other") do
        with_stubbed_externals do
          pkg = FakePackage.new("foo", board_list: ["test-board"])
          pkgmgr.register(pkg)
          result = pkg.install_impl(Ver("1.0.0"))
          assert_equal false, result
          assert_empty FakePackage.install_log
        end
      end
    end
  end

  def test_install_unknown_package_returns_false
    reset_pkgmgr!
    result = pkgmgr.install("nonexistent")
    assert_equal false, result
  end

  def test_install_arch_mismatch_returns_false
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo",
                              arch_list: { "riscv64" => ALL_ARCHS["riscv64"] })
        pkgmgr.register(pkg)
        with_context(ARCH: ALL_ARCHS["i386"]) do
          result = pkgmgr.install("foo")
          assert_equal false, result
        end
      end
    end
  end
end

class TestPackageNeedsUpgrade < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_not_installed
    with_fake_tc do
      pkg = FakePackage.new("foo")
      refute pkg.needs_upgrade?
    end
  end

  def test_current_version_installed
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")  # installs at default_ver (1.0.0)
        refute pkg.needs_upgrade?
      end
    end
  end

  def test_old_version_installed
    with_fake_tc do |tc|
      # Manually create an old version directory to simulate a
      # previous install at a different version.
      gcc_ver = ARCH.gcc_ver.to_s
      old_dir = tc / "gcc-#{gcc_ver}" / ARCH.name / "foo" / "0.9.0"
      FileUtils.mkdir_p(old_dir)

      pkg = FakePackage.new("foo")
      pkgmgr.register(pkg)
      assert pkg.needs_upgrade?
    end
  end
end

class TestPackageConfigure < Minitest::Test
  include TestHelper

  def test_not_configurable_by_default
    refute FakePackage.new("foo").configurable?
  end
end
