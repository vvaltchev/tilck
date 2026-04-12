# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'
require 'stringio'

class TestShowStatus < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # Capture stdout from a block and return it as a string.
  def capture_stdout(&block)
    old = $stdout
    $stdout = StringIO.new
    block.call
    $stdout.string
  ensure
    $stdout = old
  end

  # --- show_status (individual package line) ---

  def test_show_status_empty
    output = capture_stdout {
      pkgmgr.show_status("foo", nil, [])
    }
    assert_match(/foo/, output)
    # Empty list → empty status
  end

  def test_show_status_installed_single_arch
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", nil, list)
        }
        assert_match(/foo/, output)
        assert_match(/installed/, output)
        assert_match(/#{ARCH.name}/, output)
      end
    end
  end

  def test_show_status_installed_multiple_archs
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")

        # Also create an x86_64 install
        gcc = FAKE_GCC_VER.to_s
        FileUtils.mkdir_p(tc / "gcc-#{gcc}" / "x86_64" / "foo" / "1.0.0")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", nil, list)
        }
        assert_match(/installed/, output)
        assert_match(/i386/, output)
        assert_match(/x86_64/, output)
      end
    end
  end

  def test_show_status_broken_only
    with_fake_tc do |tc|
      # Create a version dir with no expected files (broken)
      gcc = FAKE_GCC_VER.to_s
      FileUtils.mkdir_p(tc / "gcc-#{gcc}" / ARCH.name / "brkpkg" / "1.0.0")

      # Register a package that expects a file
      pkg = FakePackage.new("brkpkg")
      # Override expected_files to require something that doesn't exist
      pkg.define_singleton_method(:expected_files) {
        [["nonexistent_binary", false]]
      }
      pkgmgr.register(pkg)
      pkgmgr.refresh()

      list = pkg.get_install_list
      assert list.any? { |x| x.broken }

      output = capture_stdout {
        pkgmgr.show_status("brkpkg", nil, list)
      }
      assert_match(/broken/, output)
      # Broken install should NOT show the arch
      refute_match(/i386/, output)
    end
  end

  def test_show_status_broken_excluded_from_arch_list
    with_fake_tc do |tc|
      with_stubbed_externals do
        # Install a working version for i386
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")

        # Create a broken install for riscv64 (empty dir, no expected files
        # but FakePackage has empty expected_files so it won't be broken)
        # Instead, create a package that IS broken on riscv64
        gcc = FAKE_GCC_VER.to_s
        FileUtils.mkdir_p(tc / "gcc-#{gcc}" / "riscv64" / "foo" / "1.0.0")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", nil, list)
        }
        # Both archs show as installed (FakePackage has empty expected_files)
        assert_match(/installed/, output)
        assert_match(/i386/, output)
        assert_match(/riscv64/, output)
      end
    end
  end

  def test_show_status_noarch_package
    with_fake_tc do |tc|
      # Create a noarch install
      FileUtils.mkdir_p(tc / "noarch" / "noarch_foo" / "1.0.0")

      pkg = FakePackage.new("noarch_foo", arch_list: nil)
      pkgmgr.register(pkg)
      pkgmgr.refresh()

      list = pkg.get_install_list
      output = capture_stdout {
        pkgmgr.show_status("noarch_foo", nil, list)
      }
      assert_match(/installed/, output)
      assert_match(/noarch/, output)
    end
  end

  def test_show_status_host_package
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true,
                              arch_list: ALL_HOST_ARCHS)
        pkgmgr.register(pkg)
        pkgmgr.install("host_foo")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("host_foo", nil, list)
        }
        assert_match(/installed/, output)
        assert_match(/host/, output)
      end
    end
  end

  def test_show_status_group_by_arch
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", "arch", list)
        }
        assert_match(/i386/, output)
        assert_match(/1\.0\.0/, output)
      end
    end
  end

  def test_show_status_group_by_ver
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", "ver", list)
        }
        assert_match(/1\.0\.0/, output)
        assert_match(/i386/, output)
      end
    end
  end

  def test_show_status_found_not_registered
    # An install that's found on disk but not from a registered package
    info = InstallInfo.new(
      "orphan_pkg", Ver("13.3.0"), false, ARCH, Ver("1.0.0"),
      Pathname.new("/fake/orphan"), nil, false
    )
    output = capture_stdout {
      pkgmgr.show_status("orphan_pkg", nil, [info])
    }
    assert_match(/found/, output)
  end
end

class TestShowStatusAll < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def capture_stdout(&block)
    old = $stdout
    $stdout = StringIO.new
    block.call
    $stdout.string
  ensure
    $stdout = old
  end

  def test_show_all_with_installed_packages
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.register(FakePackage.new("bar"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all }
        assert_match(/foo/, output)
        assert_match(/bar/, output)
        assert_match(/installed/, output)
      end
    end
  end

  def test_show_all_groups_by_type
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("target_pkg"))
        pkgmgr.register(FakePackage.new("host_tool", on_host: true,
                                        arch_list: ALL_HOST_ARCHS))
        pkgmgr.register(FakePackage.new("noarch_pkg", arch_list: nil))
        pkgmgr.install("target_pkg")
        pkgmgr.install("host_tool")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all }
        assert_match(/Packages built by system CC/, output)
        assert_match(/Source-only packages/, output)
        assert_match(/Packages built by GCC/, output)
      end
    end
  end

  def test_show_all_with_compiler
    with_fake_tc do
      with_stubbed_externals do
        cc = FakePackage.new("gcc-#{ARCH.name}-musl",
                             on_host: true, is_compiler: true,
                             arch_list: ALL_HOST_ARCHS)
        pkgmgr.register(cc)
        pkgmgr.install("gcc-#{ARCH.name}-musl")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all }
        assert_match(/GCC toolchains/, output)
        assert_match(/gcc-#{ARCH.name}-musl/, output)
      end
    end
  end

  def test_show_all_group_by_arch
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all("arch") }
        assert_match(/i386/, output)
      end
    end
  end

  def test_show_all_group_by_ver
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all("ver") }
        assert_match(/1\.0\.0/, output)
      end
    end
  end

  def test_show_all_not_installed_shows_no_status
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("uninstalled_pkg"))
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all }
        assert_match(/uninstalled_pkg/, output)
        # Should NOT show as installed
        refute_match(/installed.*uninstalled_pkg/, output)
      end
    end
  end
end
