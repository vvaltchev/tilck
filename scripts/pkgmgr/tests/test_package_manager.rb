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
    pkgmgr.register(FakePackage.new("foo"))
    assert_raises(NameError) { pkgmgr.register(FakePackage.new("foo")) }
  end
end

class TestPackageManagerDepGraph < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_no_deps
    pkgmgr.register(FakePackage.new("a"))
    pkgmgr.register(FakePackage.new("b"))
    graph = pkgmgr.build_dep_graph
    assert_equal [], graph["a"]
    assert_equal [], graph["b"]
  end

  def test_explicit_deps
    pkgmgr.register(FakePackage.new("a", dep_list: [Dep("b", false)]))
    pkgmgr.register(FakePackage.new("b"))
    graph = pkgmgr.build_dep_graph
    assert_includes graph["a"], "b"
  end

  def test_implicit_compiler_dep_for_target_pkg
    cc_name = "gcc-#{ARCH.name}-musl"
    pkgmgr.register(FakePackage.new(cc_name, on_host: true,
                                    is_compiler: true,
                                    arch_list: ALL_HOST_ARCHS))
    pkgmgr.register(FakePackage.new("foo"))
    graph = pkgmgr.build_dep_graph
    assert_includes graph["foo"], cc_name
  end

  def test_no_implicit_compiler_for_host_pkg
    pkgmgr.register(FakePackage.new("host_foo", on_host: true,
                                    arch_list: ALL_HOST_ARCHS))
    graph = pkgmgr.build_dep_graph
    assert_equal [], graph["host_foo"]
  end

  def test_no_implicit_compiler_for_noarch_pkg
    pkgmgr.register(FakePackage.new("noarch_foo", arch_list: nil))
    graph = pkgmgr.build_dep_graph
    assert_equal [], graph["noarch_foo"]
  end

  def test_validate_clean
    pkgmgr.register(FakePackage.new("a", dep_list: [Dep("b", false)]))
    pkgmgr.register(FakePackage.new("b"))
    pkgmgr.validate_deps
  end

  def test_validate_missing_dep
    pkgmgr.register(FakePackage.new("a", dep_list: [Dep("missing", false)]))
    assert_raises(DepResolver::MissingDepError) { pkgmgr.validate_deps }
  end
end

class TestPackageManagerInstall < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_install_success
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        assert pkgmgr.install("foo")
        assert_equal ["foo"], FakePackage.install_log
      end
    end
  end

  def test_install_unknown_returns_false
    result = pkgmgr.install("nonexistent")
    assert_equal false, result
  end

  def test_install_arch_mismatch
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo",
          arch_list: { "riscv64" => ALL_ARCHS["riscv64"] }))
        with_context(ARCH: ALL_ARCHS["i386"]) do
          assert_equal false, pkgmgr.install("foo")
        end
      end
    end
  end
end

class TestPackageManagerResolve < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_simple_resolve
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a"))
        pkgmgr.register(FakePackage.new("b"))
        plan = pkgmgr.resolve_install_plan([["a", nil], ["b", nil]])
        names = plan.map(&:first)
        assert_includes names, "a"
        assert_includes names, "b"
      end
    end
  end

  def test_resolve_with_deps
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a", dep_list: [Dep("b", false)]))
        pkgmgr.register(FakePackage.new("b"))
        plan = pkgmgr.resolve_install_plan([["a", nil]])
        assert_equal ["b", "a"], plan.map(&:first)
      end
    end
  end

  def test_resolve_skips_installed
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("a")
        pkgmgr.register(pkg)
        pkgmgr.install("a")
        plan = pkgmgr.resolve_install_plan([["a", nil]])
        assert_empty plan
      end
    end
  end

  def test_resolve_compiler_before_target
    with_fake_tc do
      with_stubbed_externals do
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

  def test_filtered_by_arch
    with_context(ARCH: ALL_ARCHS["i386"]) do
      pkgmgr.register(FakePackage.new("rv_only", default: true,
        arch_list: { "riscv64" => ALL_ARCHS["riscv64"] }))
      assert_empty pkgmgr.get_default_packages
    end
  end

  def test_filtered_by_board
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
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        assert_empty pkgmgr.get_upgradable_packages
      end
    end
  end

  def test_upgradable_when_old_version
    with_fake_tc do |tc|
      # Simulate an old version on disk
      gcc_ver = ARCH.gcc_ver.to_s
      old_dir = tc / "gcc-#{gcc_ver}" / ARCH.name / "foo" / "0.9.0"
      FileUtils.mkdir_p(old_dir)

      pkg = FakePackage.new("foo")
      pkgmgr.register(pkg)
      upgrades = pkgmgr.get_upgradable_packages
      assert_equal ["foo"], upgrades.map(&:name)
    end
  end

  def test_not_upgradable_when_not_installed
    with_fake_tc do
      pkgmgr.register(FakePackage.new("foo"))
      assert_empty pkgmgr.get_upgradable_packages
    end
  end

  def test_filtered_by_arch
    with_fake_tc do |tc|
      # Create old install for riscv64
      gcc_ver = ARCH.gcc_ver.to_s
      FileUtils.mkdir_p(tc / "gcc-#{gcc_ver}" / "riscv64" / "foo" / "0.9.0")

      pkg = FakePackage.new("foo",
        arch_list: { "riscv64" => ALL_ARCHS["riscv64"] })
      pkgmgr.register(pkg)

      with_context(ARCH: ALL_ARCHS["i386"]) do
        assert_empty pkgmgr.get_upgradable_packages
      end
    end
  end
end
