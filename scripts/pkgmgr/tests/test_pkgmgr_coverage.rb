# SPDX-License-Identifier: BSD-2-Clause
#
# Tests targeting specific uncovered lines in package_manager.rb:
# register edge cases, get_tc, get_installed_compilers, uninstall
# edge cases, scan_toolchain, show_status edge cases.
#

require_relative 'test_helper'
require 'stringio'

class TestRegisterEdgeCases < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_register_non_package_raises
    assert_raises(ArgumentError) { pkgmgr.register("not a package") }
  end

  def test_get_tc
    with_fake_tc do
      with_stubbed_externals do
        cc = FakePackage.new("gcc-i386-musl", on_host: true,
                             is_compiler: true, arch_list: ALL_HOST_ARCHS)
        pkgmgr.register(cc)
        assert_equal cc, pkgmgr.get_tc("i386")
        assert_nil pkgmgr.get_tc("aarch64")
      end
    end
  end
end

class TestGetInstalledCompilers < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_finds_installed_compiler
    with_fake_tc do |tc|
      with_stubbed_externals do
        cc = FakePackage.new("gcc-#{ARCH.name}-musl",
                             on_host: true, is_compiler: true,
                             portable: true, arch_list: ALL_HOST_ARCHS)
        # Match real GccCompiler: default_ver returns gcc_ver, and
        # get_install_list includes target_arch/libc.
        cc.define_singleton_method(:default_ver) { FAKE_GCC_VER }
        target = ARCH
        cc.define_singleton_method(:get_install_list) {
          super().map { |info|
            InstallInfo.new(
              info.pkgname, info.compiler, info.on_host, info.arch,
              info.ver, info.path, info.pkg, info.broken,
              target, "musl"
            )
          }
        }
        pkgmgr.register(cc)
        pkgmgr.install("gcc-#{ARCH.name}-musl")
        pkgmgr.refresh()

        compilers = pkgmgr.get_installed_compilers
        assert_equal 1, compilers.length
        assert_equal "gcc-#{ARCH.name}-musl", compilers.first.pkgname
        assert compilers.first.compiler?
      end
    end
  end

  def test_empty_when_no_compilers
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()
        assert_empty pkgmgr.get_installed_compilers
      end
    end
  end
end

class TestUninstallEdgeCases < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_uninstall_blank_name_raises
    assert_raises(ArgumentError) {
      pkgmgr.uninstall("", false, false)
    }
  end

  def test_uninstall_nil_name_raises
    assert_raises(ArgumentError) {
      pkgmgr.uninstall(nil, false, false)
    }
  end

  def test_uninstall_version_not_installed_falls_back_to_all
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")  # installs 1.0.0

        # Also create a manual 2.0.0 install
        gcc = FAKE_GCC_VER.to_s
        FileUtils.mkdir_p(
          tc / "gcc-#{gcc}" / ARCH.name / "foo" / "2.0.0"
        )
        pkgmgr.refresh()

        # Uninstall with default ver — 1.0.0 IS installed, so only
        # 1.0.0 is removed
        pkgmgr.uninstall("foo", false, false)
        v2 = tc / "gcc-#{gcc}" / ARCH.name / "foo" / "2.0.0"
        assert v2.directory?

        # Now 1.0.0 is gone. Uninstall again — default ver (1.0.0)
        # is NOT installed, falls back to all_ver, removes 2.0.0
        pkgmgr.refresh()
        pkgmgr.uninstall("foo", false, false)
        refute v2.exist?
      end
    end
  end
end

class TestScanToolchain < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def capture_stderr(&block)
    old = $stderr
    $stderr = StringIO.new
    block.call
    $stderr.string
  ensure
    $stderr = old
  end

  def test_scan_finds_orphan_packages
    with_fake_tc do |tc|
      with_stubbed_externals do
        # Create a package on disk that's NOT registered
        gcc = FAKE_GCC_VER.to_s
        orphan_dir = tc / "gcc-#{gcc}" / ARCH.name / "orphan_pkg" / "1.0.0"
        FileUtils.mkdir_p(orphan_dir)

        pkgmgr.refresh()

        # The orphan should show up in found_installed
        found = pkgmgr.instance_variable_get(:@found_installed)
        orphans = found.select { |x| x.pkgname == "orphan_pkg" }
        assert_equal 1, orphans.length
        assert_equal Ver("1.0.0"), orphans.first.ver
      end
    end
  end

  def test_scan_finds_noarch_orphans
    with_fake_tc do |tc|
      with_stubbed_externals do
        FileUtils.mkdir_p(tc / "noarch" / "some_src" / "2.0.0")
        pkgmgr.refresh()

        found = pkgmgr.instance_variable_get(:@found_installed)
        orphans = found.select { |x| x.pkgname == "some_src" }
        assert_equal 1, orphans.length
      end
    end
  end

  def test_scan_finds_host_portable_orphans
    with_fake_tc do |tc|
      with_stubbed_externals do
        FileUtils.mkdir_p(HOST_DIR_PORTABLE / "some_tool" / "3.0.0")
        pkgmgr.refresh()

        found = pkgmgr.instance_variable_get(:@found_installed)
        orphans = found.select { |x| x.pkgname == "some_tool" }
        assert_equal 1, orphans.length
        assert_equal "syscc", orphans.first.compiler
      end
    end
  end

  def test_scan_finds_host_distro_orphans
    with_fake_tc do |tc|
      with_stubbed_externals do
        FileUtils.mkdir_p(HOST_DIR / "host_thing" / "1.0.0")
        pkgmgr.refresh()

        found = pkgmgr.instance_variable_get(:@found_installed)
        orphans = found.select { |x| x.pkgname == "host_thing" }
        assert_equal 1, orphans.length
      end
    end
  end

  def test_scan_skips_invalid_version_dirs
    with_fake_tc do |tc|
      with_stubbed_externals do
        gcc = FAKE_GCC_VER.to_s
        # Create a dir with an unparseable version name
        FileUtils.mkdir_p(
          tc / "gcc-#{gcc}" / ARCH.name / "pkg" / "not_a_version!!"
        )
        pkgmgr.refresh()

        found = pkgmgr.instance_variable_get(:@found_installed)
        bad = found.select { |x| x.pkgname == "pkg" }
        assert_empty bad  # should be skipped, not crash
      end
    end
  end

  def test_scan_skips_invalid_gcc_dir
    with_fake_tc do |tc|
      with_stubbed_externals do
        # Create gcc-bad_version/ which can't be parsed
        FileUtils.mkdir_p(tc / "gcc-not_a_version!!" / ARCH.name)
        pkgmgr.refresh()  # should not crash
      end
    end
  end

  def test_scan_skips_unknown_arch
    with_fake_tc do |tc|
      with_stubbed_externals do
        gcc = FAKE_GCC_VER.to_s
        FileUtils.mkdir_p(tc / "gcc-#{gcc}" / "mips" / "foo" / "1.0.0")
        pkgmgr.refresh()

        found = pkgmgr.instance_variable_get(:@found_installed)
        mips = found.select { |x| x.pkgname == "foo" }
        assert_empty mips  # mips is not in ALL_ARCHS
      end
    end
  end
end

class TestShowStatusEdgeCases < Minitest::Test
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

  def test_show_status_nil_list
    output = capture_stdout {
      pkgmgr.show_status("foo", nil, nil)
    }
    assert_match(/foo/, output)
  end

  def test_show_status_found_but_not_registered
    # An orphan install has pkg=nil — should show "found" not "installed"
    info = InstallInfo.new(
      "orphan", FAKE_GCC_VER, false, ARCH, Ver("1.0.0"),
      Pathname.new("/fake/path"), nil, false
    )
    output = capture_stdout {
      pkgmgr.show_status("orphan", nil, [info])
    }
    assert_match(/found/, output)
  end

  def test_show_status_not_registered_no_path
    # Installable-only entry (no path, no pkg)
    info = InstallInfo.new(
      "ghost", nil, false, nil, Ver("1.0.0"),
      nil, nil, false
    )
    output = capture_stdout {
      pkgmgr.show_status("ghost", nil, [info])
    }
    # No path, no pkg → empty status
    refute_match(/installed/, output)
    refute_match(/found/, output)
  end

  def test_show_status_all_with_all_compilers_flag
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        output = capture_stdout {
          pkgmgr.show_status_all(nil, true)  # all_compilers = true
        }
        assert_match(/foo/, output)
      end
    end
  end
end
