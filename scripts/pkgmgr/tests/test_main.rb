# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'
require_relative '../main'

# ---------------------------------------------------------------
# Tests for CLI option parsing (Main.parse_options).
# These are pure — no filesystem, no installs, just argv in → opts out.
# ---------------------------------------------------------------

class TestParseOptionsBasic < Minitest::Test

  def test_no_args
    opts = Main.parse_options([])
    assert_empty opts[:install]
    assert_empty opts[:uninstall]
    refute opts[:list]
    refute opts[:self_test]
    refute opts[:upgrade]
  end

  def test_list
    opts = Main.parse_options(["-l"])
    assert opts[:list]
  end

  def test_self_test
    opts = Main.parse_options(["-t"])
    assert opts[:self_test]
  end

  def test_coverage
    opts = Main.parse_options(["--coverage"])
    assert opts[:coverage]
  end

  def test_upgrade
    opts = Main.parse_options(["--upgrade"])
    assert opts[:upgrade]
  end

  def test_check_for_updates
    opts = Main.parse_options(["--check-for-updates"])
    assert opts[:check_for_updates]
  end

  def test_just_context
    opts = Main.parse_options(["-j"])
    assert opts[:just_context]
  end

  def test_dry_run
    opts = Main.parse_options(["-u", "foo", "-d"])
    assert opts[:dry_run]
  end

  def test_force
    opts = Main.parse_options(["-u", "ALL", "-f"])
    assert opts[:force]
  end

  def test_quiet
    opts = Main.parse_options(["-q"])
    assert_equal 1, opts[:quiet]
  end

  def test_skip_install_pkgs
    opts = Main.parse_options(["-n"])
    assert opts[:skip_install_pkgs]
  end
end

class TestParseOptionsInstall < Minitest::Test

  def test_single_package
    opts = Main.parse_options(["-s", "busybox"])
    assert_equal ["busybox"], opts[:install]
  end

  def test_multiple_packages
    opts = Main.parse_options(["-s", "busybox", "zlib", "vim"])
    assert_equal ["busybox", "zlib", "vim"], opts[:install]
  end

  def test_version_pinned
    opts = Main.parse_options(["-s", "busybox:1.36.1"])
    assert_equal ["busybox:1.36.1"], opts[:install]
  end

  def test_install_compiler
    opts = Main.parse_options(["-S", "i386"])
    # -S appends "gcc-<arch>-musl:<ver>" where ver may be nil
    assert opts[:install].any? { |s| s.start_with?("gcc-i386-musl") }
  end

  def test_install_compiler_unknown_arch
    assert_raises(OptionParser::InvalidArgument) {
      Main.parse_options(["-S", "mips"])
    }
  end
end

class TestParseOptionsUninstall < Minitest::Test

  def test_single_package
    opts = Main.parse_options(["-u", "busybox"])
    assert_equal ["busybox"], opts[:uninstall]
  end

  def test_uninstall_compiler
    opts = Main.parse_options(["-U", "riscv64"])
    assert opts[:uninstall].any? { |s| s.start_with?("gcc-riscv64-musl") }
  end

  def test_compiler_ver_filter
    opts = Main.parse_options(["-u", "ALL", "-c", "ALL"])
    assert_equal "ALL", opts[:compiler]
  end

  def test_compiler_ver_syscc
    opts = Main.parse_options(["-u", "foo", "-c", "syscc"])
    assert_equal "syscc", opts[:compiler]
  end

  def test_arch_filter
    opts = Main.parse_options(["-u", "ALL", "-a", "ALL"])
    assert_equal "ALL", opts[:arch]
  end

  def test_arch_filter_specific
    opts = Main.parse_options(["-u", "foo", "-a", "i386"])
    assert_equal "i386", opts[:arch]
  end

  def test_unknown_arch_raises
    assert_raises(OptionParser::InvalidArgument) {
      Main.parse_options(["-u", "foo", "-a", "mips"])
    }
  end
end

class TestParseOptionsConfig < Minitest::Test

  def test_config_package
    opts = Main.parse_options(["-C", "busybox"])
    assert_equal "busybox", opts[:config]
  end
end

class TestParseOptionsMutualExclusion < Minitest::Test

  def test_two_modes_raises
    assert_raises(OptionParser::InvalidArgument) {
      Main.parse_options(["-l", "-t"])
    }
  end

  def test_install_and_uninstall_raises
    assert_raises(OptionParser::InvalidArgument) {
      Main.parse_options(["-s", "foo", "-u", "bar"])
    }
  end

  def test_list_with_compiler_not_ALL_raises
    assert_raises(OptionParser::InvalidArgument) {
      Main.parse_options(["-l", "-c", "13.3.0"])
    }
  end

  def test_list_with_compiler_ALL_ok
    opts = Main.parse_options(["-l", "-c", "ALL"])
    assert opts[:list]
    assert_equal "ALL", opts[:compiler]
  end
end

class TestParseOptionsGroupBy < Minitest::Test

  def test_group_by_ver
    opts = Main.parse_options(["-l", "-g", "ver"])
    assert_equal "ver", opts[:group_by]
  end

  def test_group_by_arch
    opts = Main.parse_options(["-l", "-g", "arch"])
    assert_equal "arch", opts[:group_by]
  end

  def test_group_by_invalid_raises
    assert_raises(OptionParser::InvalidArgument) {
      Main.parse_options(["-l", "-g", "invalid"])
    }
  end
end

# ---------------------------------------------------------------
# Integration tests for Main.main() with fake TC and stubs.
# ---------------------------------------------------------------

class TestMainIntegration < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_check_for_updates_no_upgrades
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo", default: true)
        pkgmgr.register(pkg)
        pkgmgr.install("foo")

        result = Main.main(["--check-for-updates"])
        assert_equal 0, result
      end
    end
  end

  def test_check_for_updates_with_upgrade_needed
    with_fake_tc do |tc|
      with_stubbed_externals do
        gcc_ver = FAKE_GCC_VER.to_s
        FileUtils.mkdir_p(tc / "gcc-#{gcc_ver}" / ARCH.name / "foo" / "0.9.0")

        pkgmgr.register(FakePackage.new("foo"))

        result = Main.main(["--check-for-updates"])
        assert_equal 2, result
      end
    end
  end

  def test_install_single_package
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))

        result = Main.main(["-s", "foo"])
        assert_equal 0, result
        assert_equal ["foo"], FakePackage.install_log
      end
    end
  end

  def test_install_with_deps
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a",
          dep_list: [Dep("b", false)]))
        pkgmgr.register(FakePackage.new("b"))

        result = Main.main(["-s", "a"])
        assert_equal 0, result
        assert_equal ["b", "a"], FakePackage.install_log
      end
    end
  end

  def test_install_unknown_package
    with_fake_tc do
      with_stubbed_externals do
        result = Main.main(["-s", "nonexistent"])
        assert_equal 1, result
      end
    end
  end

  def test_upgrade_mode
    with_fake_tc do |tc|
      with_stubbed_externals do
        gcc_ver = FAKE_GCC_VER.to_s
        FileUtils.mkdir_p(
          tc / "gcc-#{gcc_ver}" / ARCH.name / "foo" / "0.9.0"
        )
        pkgmgr.register(FakePackage.new("foo"))

        result = Main.main(["--upgrade"])
        assert_equal 0, result
        assert_includes FakePackage.install_log, "foo"
      end
    end
  end

  def test_upgrade_nothing_to_do
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        FakePackage.clear_log!

        result = Main.main(["--upgrade"])
        assert_equal 0, result
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_default_install_mode
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("dflt", default: true))
        pkgmgr.register(FakePackage.new("opt"))

        result = Main.main([])
        assert_equal 0, result
        assert_includes FakePackage.install_log, "dflt"
        refute_includes FakePackage.install_log, "opt"
      end
    end
  end

  def test_config_non_configurable
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))

        result = Main.main(["-C", "foo"])
        assert_equal 1, result
      end
    end
  end

  def test_config_unknown_package
    with_fake_tc do
      with_stubbed_externals do
        result = Main.main(["-C", "nonexistent"])
        assert_equal 1, result
      end
    end
  end
end
