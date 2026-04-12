# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

class TestPackageManagerRegister < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_register_and_get
    pkg = FakePackage.new("foo")
    pkgmgr.register(pkg)
    assert_equal pkg, pkgmgr.get("foo")
  end

  def test_get_nonexistent
    assert_nil pkgmgr.get("nonexistent")
  end

  def test_register_duplicate_raises
    pkg1 = FakePackage.new("foo")
    pkg2 = FakePackage.new("foo")
    pkgmgr.register(pkg1)
    assert_raises(NameError) { pkgmgr.register(pkg2) }
  end
end

class TestPackageManagerDepGraph < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_build_dep_graph_no_deps
    pkgmgr.register(FakePackage.new("a"))
    pkgmgr.register(FakePackage.new("b"))
    graph = pkgmgr.build_dep_graph
    assert_equal [], graph["a"]
    assert_equal [], graph["b"]
  end

  def test_build_dep_graph_with_deps
    pkgmgr.register(FakePackage.new("a", dep_list: [Dep("b", false)]))
    pkgmgr.register(FakePackage.new("b"))
    graph = pkgmgr.build_dep_graph
    assert_includes graph["a"], "b"
    assert_equal [], graph["b"]
  end

  def test_build_dep_graph_implicit_compiler_dep
    # Register a fake compiler for the current ARCH
    cc_name = "gcc-#{ARCH.name}-musl"
    pkgmgr.register(FakePackage.new(cc_name, on_host: true,
                                    is_compiler: true,
                                    arch_list: ALL_HOST_ARCHS))
    # Register a regular target package
    pkgmgr.register(FakePackage.new("foo"))
    graph = pkgmgr.build_dep_graph
    assert_includes graph["foo"], cc_name
  end

  def test_build_dep_graph_host_pkg_no_implicit_compiler
    pkgmgr.register(FakePackage.new("host_foo", on_host: true,
                                    arch_list: ALL_HOST_ARCHS))
    graph = pkgmgr.build_dep_graph
    assert_equal [], graph["host_foo"]
  end

  def test_build_dep_graph_noarch_no_implicit_compiler
    pkgmgr.register(FakePackage.new("noarch_foo", arch_list: nil))
    graph = pkgmgr.build_dep_graph
    assert_equal [], graph["noarch_foo"]
  end

  def test_validate_deps_clean
    pkgmgr.register(FakePackage.new("a", dep_list: [Dep("b", false)]))
    pkgmgr.register(FakePackage.new("b"))
    pkgmgr.validate_deps  # should not raise
  end

  def test_validate_deps_missing
    pkgmgr.register(FakePackage.new("a", dep_list: [Dep("missing", false)]))
    assert_raises(DepResolver::MissingDepError) {
      pkgmgr.validate_deps
    }
  end
end

class TestPackageManagerInstall < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
    FakePackage.clear_fail_set!
  end

  def test_install_by_name
    pkgmgr.register(FakePackage.new("foo"))
    result = pkgmgr.install("foo")
    assert result
    assert_equal ["foo"], FakePackage.install_log
  end

  def test_install_unknown_package
    # get_smart returns nil for unknown name, install returns false
    result = pkgmgr.install("nonexistent")
    assert_equal false, result
  end

  def test_install_arch_mismatch
    pkg = FakePackage.new("foo",
                          arch_list: { "riscv64" => ALL_ARCHS["riscv64"] })
    pkgmgr.register(pkg)
    with_context(ARCH: ALL_ARCHS["i386"]) do
      result = pkgmgr.install("foo")
      assert_equal false, result
    end
  end
end

class TestPackageManagerResolve < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_resolve_simple
    pkgmgr.register(FakePackage.new("a"))
    pkgmgr.register(FakePackage.new("b"))
    plan = pkgmgr.resolve_install_plan([["a", nil], ["b", nil]])
    names = plan.map(&:first)
    assert_includes names, "a"
    assert_includes names, "b"
  end

  def test_resolve_with_deps
    pkgmgr.register(FakePackage.new("a", dep_list: [Dep("b", false)]))
    pkgmgr.register(FakePackage.new("b"))
    plan = pkgmgr.resolve_install_plan([["a", nil]])
    names = plan.map(&:first)
    assert_equal ["b", "a"], names
  end

  def test_resolve_skips_installed
    pkg_a = FakePackage.new("a")
    pkg_a.fake_installed_versions.add(pkg_a.default_ver)
    pkgmgr.register(pkg_a)
    plan = pkgmgr.resolve_install_plan([["a", nil]])
    assert_empty plan
  end

  def test_resolve_install_order_with_compiler
    cc_name = "gcc-#{ARCH.name}-musl"
    pkgmgr.register(FakePackage.new(cc_name, on_host: true,
                                    is_compiler: true,
                                    arch_list: ALL_HOST_ARCHS))
    pkgmgr.register(FakePackage.new("foo"))
    plan = pkgmgr.resolve_install_plan([
      [cc_name, nil], ["foo", nil]
    ])
    names = plan.map(&:first)
    assert_operator names.index(cc_name), :<, names.index("foo")
  end
end

class TestPackageManagerDefaults < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_get_default_packages
    pkgmgr.register(FakePackage.new("default_pkg", default: true))
    pkgmgr.register(FakePackage.new("optional_pkg"))
    defaults = pkgmgr.get_default_packages
    assert_equal 1, defaults.length
    assert_equal "default_pkg", defaults.first.name
  end

  def test_default_packages_filtered_by_arch
    pkgmgr.register(FakePackage.new("rv_only", default: true,
      arch_list: { "riscv64" => ALL_ARCHS["riscv64"] }))
    with_context(ARCH: ALL_ARCHS["i386"]) do
      assert_empty pkgmgr.get_default_packages
    end
  end

  def test_default_packages_filtered_by_board
    with_context(BOARD: "other") do
      pkgmgr.register(FakePackage.new("board_pkg", default: true,
                                      board_list: ["test-board"]))
      assert_empty pkgmgr.get_default_packages
    end
  end
end

class TestPackageManagerUpgrade < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_no_upgradable_when_current
    pkg = FakePackage.new("foo", default: true)
    pkg.fake_installed_versions.add(pkg.default_ver)
    pkgmgr.register(pkg)
    assert_empty pkgmgr.get_upgradable_packages
  end

  def test_upgradable_when_old_version
    pkg = FakePackage.new("foo")
    pkg.fake_installed_versions.add(Ver("0.0.1"))
    pkgmgr.register(pkg)
    upgrades = pkgmgr.get_upgradable_packages
    assert_equal 1, upgrades.length
    assert_equal "foo", upgrades.first.name
  end

  def test_not_upgradable_when_not_installed
    pkgmgr.register(FakePackage.new("foo"))
    assert_empty pkgmgr.get_upgradable_packages
  end

  def test_upgradable_filtered_by_arch
    pkg = FakePackage.new("foo",
      arch_list: { "riscv64" => ALL_ARCHS["riscv64"] })
    pkg.fake_installed_versions.add(Ver("0.0.1"))
    pkgmgr.register(pkg)
    with_context(ARCH: ALL_ARCHS["i386"]) do
      assert_empty pkgmgr.get_upgradable_packages
    end
  end
end
