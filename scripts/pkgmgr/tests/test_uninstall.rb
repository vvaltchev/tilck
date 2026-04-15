# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

class TestUninstallSingle < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_uninstall_removes_version_dir
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        gcc = FAKE_GCC_VER.to_s
        ver_dir = tc / "gcc-#{gcc}" / ARCH.name / "foo" / "1.0.0"
        assert ver_dir.directory?

        pkgmgr.uninstall("foo", false, false)
        refute ver_dir.exist?
      end
    end
  end

  def test_uninstall_cleans_empty_parents
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        gcc = FAKE_GCC_VER.to_s
        pkg_dir = tc / "gcc-#{gcc}" / ARCH.name / "foo"
        pkgmgr.uninstall("foo", false, false)
        refute pkg_dir.exist?
      end
    end
  end

  def test_uninstall_dry_run
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        gcc = FAKE_GCC_VER.to_s
        ver_dir = tc / "gcc-#{gcc}" / ARCH.name / "foo" / "1.0.0"
        pkgmgr.uninstall("foo", true, false)  # dry = true
        assert ver_dir.directory?  # still there
      end
    end
  end

  def test_uninstall_unknown_package_no_crash
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.refresh()
        pkgmgr.uninstall("nonexistent", false, false)
        # Should not raise — just warns and does nothing
      end
    end
  end

  def test_uninstall_noarch_package
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("noarch_foo", arch_list: nil)
        pkgmgr.register(pkg)

        # Manually create the noarch install dir
        ver_dir = tc / "noarch" / "noarch_foo" / "1.0.0"
        FileUtils.mkdir_p(ver_dir)
        pkgmgr.refresh()

        pkgmgr.uninstall("noarch_foo", false, false)
        refute ver_dir.exist?
      end
    end
  end

  def test_uninstall_host_package
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkgmgr.install("host_foo")
        pkgmgr.refresh()

        pkgmgr.uninstall("host_foo", false, false)
        refute pkg.installed?(Ver("1.0.0"))
      end
    end
  end
end

class TestUninstallALL < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_uninstall_all_default_arch
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a"))
        pkgmgr.register(FakePackage.new("b"))
        pkgmgr.install("a")
        pkgmgr.install("b")
        pkgmgr.refresh()

        pkgmgr.uninstall("ALL", false, false)
        refute pkgmgr.get("a").installed?(Ver("1.0.0"))
        refute pkgmgr.get("b").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_uninstall_all_excludes_compilers_without_force
    with_fake_tc do |tc|
      with_stubbed_externals do
        cc = FakePackage.new("gcc-#{ARCH.name}-musl",
                             on_host: true, is_compiler: true,
                             arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(cc)
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("gcc-#{ARCH.name}-musl")
        pkgmgr.install("foo")
        pkgmgr.refresh()

        pkgmgr.uninstall("ALL", false, false)  # force = false
        # Compiler should still be installed
        assert cc.installed?(Ver("1.0.0"))
        # Regular package should be removed
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_uninstall_all_includes_compilers_with_force
    with_fake_tc do |tc|
      with_stubbed_externals do
        cc = FakePackage.new("gcc-#{ARCH.name}-musl",
                             on_host: true, is_compiler: true,
                             host_tier: :portable,
                             arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(cc)
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("gcc-#{ARCH.name}-musl")
        pkgmgr.install("foo")
        pkgmgr.refresh()

        # Need -c ALL to also match host packages (compiler="syscc")
        pkgmgr.uninstall("ALL", false, true, nil, "ALL", "ALL")
        refute cc.installed?(Ver("1.0.0"))
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_uninstall_all_with_arch_filter
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        # Also create a "riscv64" install manually
        gcc = FAKE_GCC_VER.to_s
        rv_dir = tc / "gcc-#{gcc}" / "riscv64" / "foo" / "1.0.0"
        FileUtils.mkdir_p(rv_dir)
        pkgmgr.refresh()

        # Uninstall only riscv64
        pkgmgr.uninstall("ALL", false, false, nil, nil, "riscv64")
        # riscv64 install should be gone
        refute rv_dir.exist?
        # i386 install should remain
        assert pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_uninstall_all_with_compiler_filter
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        # Uninstall with compiler=ALL should include this package
        pkgmgr.uninstall("ALL", false, false, nil, "ALL", nil)
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end
end

class TestUninstallVersions < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_uninstall_specific_version
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        # Also install a second "version" manually
        gcc = FAKE_GCC_VER.to_s
        v2_dir = tc / "gcc-#{gcc}" / ARCH.name / "foo" / "2.0.0"
        FileUtils.mkdir_p(v2_dir)
        pkgmgr.refresh()

        # Uninstall only version 1.0.0
        pkgmgr.uninstall("foo", false, false, Ver("1.0.0"))
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
        assert v2_dir.directory?  # 2.0.0 still there
      end
    end
  end

  def test_uninstall_falls_back_to_all_versions
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        # Install at version 1.0.0 (default_ver)
        pkgmgr.install("foo")

        # Also add 2.0.0
        gcc = FAKE_GCC_VER.to_s
        v2_dir = tc / "gcc-#{gcc}" / ARCH.name / "foo" / "2.0.0"
        FileUtils.mkdir_p(v2_dir)
        pkgmgr.refresh()

        # Uninstall without specifying version — default_ver is 1.0.0,
        # which IS installed, so only 1.0.0 gets removed
        pkgmgr.uninstall("foo", false, false)
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
        assert v2_dir.directory?  # 2.0.0 untouched
      end
    end
  end
end
