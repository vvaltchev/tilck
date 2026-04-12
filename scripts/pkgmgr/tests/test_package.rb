# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

class TestPackageAttributes < Minitest::Test
  include TestHelper

  def test_host_supported_no_constraints
    pkg = FakePackage.new("foo")
    assert pkg.host_supported?
  end

  def test_host_supported_matching_os
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: [HOST_OS])
    assert pkg.host_supported?
  end

  def test_host_not_supported_wrong_os
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: ["nope_os"])
    refute pkg.host_supported?
  end

  def test_host_supported_matching_arch
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_arch_list: [HOST_ARCH.name])
    assert pkg.host_supported?
  end

  def test_host_not_supported_wrong_arch
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_arch_list: ["nope_arch"])
    refute pkg.host_supported?
  end

  def test_host_supported_both_constraints
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: [HOST_OS],
                          host_arch_list: [HOST_ARCH.name])
    assert pkg.host_supported?
  end

  def test_board_supported_nil_means_any
    pkg = FakePackage.new("foo")
    assert pkg.board_supported?
  end

  def test_board_supported_matching
    with_context(BOARD: "test-board") do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      assert pkg.board_supported?
    end
  end

  def test_board_not_supported
    with_context(BOARD: "other-board") do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      refute pkg.board_supported?
    end
  end

  def test_board_nil_with_board_list
    with_context(BOARD: nil) do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      refute pkg.board_supported?
    end
  end

  def test_arch_supported_all_archs
    pkg = FakePackage.new("foo", arch_list: ALL_ARCHS)
    assert pkg.arch_supported?
  end

  def test_arch_supported_nil_means_noarch
    pkg = FakePackage.new("foo", arch_list: nil)
    assert pkg.arch_supported?
  end

  def test_arch_supported_host_package
    pkg = FakePackage.new("host_foo", on_host: true,
                          arch_list: ALL_HOST_ARCHS)
    assert pkg.arch_supported?
  end

  def test_arch_not_supported
    pkg = FakePackage.new("foo",
                          arch_list: { "riscv64" => ALL_ARCHS["riscv64"] })
    with_context(ARCH: ALL_ARCHS["i386"]) do
      refute pkg.arch_supported?
    end
  end

  def test_arch_supported_matching
    pkg = FakePackage.new("foo",
                          arch_list: { "i386" => ALL_ARCHS["i386"] })
    with_context(ARCH: ALL_ARCHS["i386"]) do
      assert pkg.arch_supported?
    end
  end
end

class TestPackageDefault < Minitest::Test
  include TestHelper

  def test_default_false_by_default
    pkg = FakePackage.new("foo")
    refute pkg.default?
  end

  def test_default_true_when_set
    pkg = FakePackage.new("foo", default: true)
    assert pkg.default?
  end

  def test_default_gated_by_arch
    pkg = FakePackage.new("foo", default: true,
                          arch_list: { "riscv64" => ALL_ARCHS["riscv64"] })
    with_context(ARCH: ALL_ARCHS["i386"]) do
      refute pkg.default?
    end
  end

  def test_default_gated_by_host_os
    pkg = FakePackage.new("host_foo", on_host: true, default: true,
                          host_os_list: ["nope_os"])
    refute pkg.default?
  end

  def test_default_gated_by_board
    with_context(BOARD: "other-board") do
      pkg = FakePackage.new("foo", default: true,
                            board_list: ["test-board"])
      refute pkg.default?
    end
  end

  def test_default_passes_all_gates
    pkg = FakePackage.new("foo", default: true,
                          arch_list: { ARCH.name => ARCH })
    assert pkg.default?
  end
end

class TestPackageUpgrade < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_needs_upgrade_not_installed
    pkg = FakePackage.new("foo")
    refute pkg.needs_upgrade?
  end

  def test_needs_upgrade_current_version
    pkg = FakePackage.new("foo")
    pkg.fake_installed_versions.add(pkg.default_ver)
    refute pkg.needs_upgrade?
  end

  def test_needs_upgrade_old_version
    pkg = FakePackage.new("foo")
    pkg.fake_installed_versions.add(Ver("0.0.1"))
    assert pkg.needs_upgrade?
  end

  def test_needs_upgrade_both_versions
    pkg = FakePackage.new("foo")
    pkg.fake_installed_versions.add(Ver("0.0.1"))
    pkg.fake_installed_versions.add(pkg.default_ver)
    refute pkg.needs_upgrade?
  end
end

class TestPackageInstall < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
    FakePackage.clear_fail_set!
  end

  def test_install_success
    pkg = FakePackage.new("foo")
    pkgmgr.register(pkg)
    result = pkg.install_impl(pkg.default_ver)
    assert result
    assert_equal ["foo"], FakePackage.install_log
  end

  def test_install_already_installed_returns_nil
    pkg = FakePackage.new("foo")
    pkg.fake_installed_versions.add(pkg.default_ver)
    pkgmgr.register(pkg)
    result = pkg.install_impl(pkg.default_ver)
    assert_nil result
    assert_empty FakePackage.install_log
  end

  def test_install_failure
    pkg = FakePackage.new("foo")
    FakePackage.fail_set.add("foo")
    pkgmgr.register(pkg)
    result = pkg.install_impl(pkg.default_ver)
    assert_equal false, result
  end

  def test_install_host_not_supported
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: ["nope_os"])
    pkgmgr.register(pkg)
    result = pkg.install_impl(pkg.default_ver)
    assert_equal false, result
    assert_empty FakePackage.install_log
  end

  def test_install_board_not_supported
    with_context(BOARD: "other") do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      pkgmgr.register(pkg)
      result = pkg.install_impl(pkg.default_ver)
      assert_equal false, result
      assert_empty FakePackage.install_log
    end
  end
end

class TestPackageConfigure < Minitest::Test
  include TestHelper

  def test_not_configurable_by_default
    pkg = FakePackage.new("foo")
    refute pkg.configurable?
  end
end
