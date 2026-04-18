# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'
require_relative '../main'
require 'stringio'

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

  def test_install_compiler_ALL_expands_to_every_arch
    # -S ALL expands to one gcc-<arch>-musl entry per registered
    # architecture in ALL_ARCHS.
    opts = Main.parse_options(["-S", "ALL"])
    names = opts[:install].map { |s| s.split(":").first }.sort
    expected = ALL_ARCHS.values.map { |a| "gcc-#{a.name}-musl" }.sort
    assert_equal expected, names
  end

  def test_install_compiler_ALL_with_version
    # Version passed to -S ALL:<ver> is propagated to every compiler.
    opts = Main.parse_options(["-S", "ALL:13.3.0"])
    vers = opts[:install].map { |s| s.split(":", 2).last }.uniq
    assert_equal ["13.3.0"], vers
  end
end

class TestExpandInstallAll < Minitest::Test
  include TestHelper

  # expand_install_all runs inside a with_target_arch scope in main().
  # Tests set up their own fake registry and call the helper directly.

  def setup
    reset_pkgmgr!
  end

  def test_install_ALL_expands_to_installable_non_compilers
    pkgmgr.register(FakePackage.new("foo"))
    pkgmgr.register(FakePackage.new("bar"))
    pkgmgr.register(
      FakePackage.new("gcc-fake-musl", on_host: true, is_compiler: true)
    )

    result = Main.expand_install_all(["ALL"])
    names = result.map { |s| s.split(":").first }.sort
    assert_equal ["bar", "foo"], names
  end

  def test_install_ALL_skips_packages_not_supported_on_current_arch
    other_arch = (ALL_ARCHS.values - [ARCH]).first
    pkgmgr.register(FakePackage.new("universal"))
    pkgmgr.register(FakePackage.new("other_only", arch_list: [other_arch]))

    result = Main.expand_install_all(["ALL"])
    names = result.map { |s| s.split(":").first }
    assert_includes names, "universal"
    refute_includes names, "other_only"
  end

  def test_install_ALL_coexists_with_named_packages
    pkgmgr.register(FakePackage.new("foo"))
    pkgmgr.register(FakePackage.new("bar"))

    result = Main.expand_install_all(["custom", "ALL"])
    names = result.map { |s| s.split(":").first }
    assert_includes names, "custom"
    assert_includes names, "foo"
    assert_includes names, "bar"
  end

  def test_install_ALL_respects_target_arch_scope
    # When with_target_arch scopes to riscv64, a package that only
    # supports riscv64 is included; an i386-only package is excluded.
    rv = ALL_ARCHS["riscv64"]
    i3 = ALL_ARCHS["i386"]
    pkgmgr.register(FakePackage.new("rv_pkg", arch_list: [rv]))
    pkgmgr.register(FakePackage.new("i3_pkg", arch_list: [i3]))
    pkgmgr.register(FakePackage.new("universal"))

    pkgmgr.with_target_arch(rv) do
      result = Main.expand_install_all(["ALL"])
      names = result.map { |s| s.split(":").first }
      assert_includes names, "rv_pkg"
      assert_includes names, "universal"
      refute_includes names, "i3_pkg"
    end
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

  def test_uninstall_compiler_ALL_expands_to_every_arch
    # -U ALL expands to one gcc-<arch>-musl entry per registered
    # architecture — symmetric with -S ALL.
    opts = Main.parse_options(["-U", "ALL"])
    names = opts[:uninstall].map { |s| s.split(":").first }.sort
    expected = ALL_ARCHS.values.map { |a| "gcc-#{a.name}-musl" }.sort
    assert_equal expected, names
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

class TestMainListMode < Minitest::Test
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

  def test_list_mode
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        output = capture_stdout { Main.main(["-l"]) }
        assert_match(/foo/, output)
        assert_match(/installed/, output)
      end
    end
  end

  def test_list_with_group_by_arch
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        output = capture_stdout { Main.main(["-l", "-g", "arch"]) }
        assert_match(/foo/, output)
        assert_match(/i386/, output)
      end
    end
  end

  def test_list_with_compiler_all
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        output = capture_stdout { Main.main(["-l", "-c", "ALL"]) }
        assert_match(/foo/, output)
      end
    end
  end
end

class TestMainDumpContext < Minitest::Test
  include TestHelper

  def capture_stdout(&block)
    old = $stdout
    $stdout = StringIO.new
    block.call
    $stdout.string
  ensure
    $stdout = old
  end

  def test_dump_context
    output = capture_stdout { Main.dump_context }
    assert_match(/MAIN_DIR/, output)
    assert_match(/TC/, output)
    assert_match(/HOST_ARCH/, output)
    assert_match(/HOST_OS/, output)
    assert_match(/ARCH/, output)
  end

  def test_just_context_mode
    with_fake_tc do
      with_stubbed_externals do
        result = Main.main(["-j"])
        assert_equal 0, result
      end
    end
  end
end

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

class TestMainDryRunInstall < Minitest::Test
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

  def test_install_dry_run_does_not_install
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))

        result = nil
        out = capture_stdout {
          result = Main.main(["-s", "foo", "-d", "--ascii"])
        }
        assert_equal 0, result
        assert_empty FakePackage.install_log
        assert_match(/Install plan:/, out)
        assert_match(/^foo$/, out)
        assert_match(/Dry run/, out)
      end
    end
  end

  def test_install_dry_run_with_deps_shows_topological_plan
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a",
          dep_list: [Dep("b", false)]))
        pkgmgr.register(FakePackage.new("b"))

        result = nil
        out = capture_stdout {
          result = Main.main(["-s", "a", "-d", "--ascii"])
        }
        assert_equal 0, result
        assert_empty FakePackage.install_log
        # ASCII tree: root "a" with child "b" indented.
        assert_match(/Install plan:/, out)
        assert_match(/^a$/, out)
        assert_match(/^  b$/, out)
      end
    end
  end

  def test_install_dry_run_with_force_does_not_remove
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        FakePackage.clear_log!

        result = nil
        out = capture_stdout {
          result = Main.main(["-s", "foo", "-f", "-d"])
        }
        assert_equal 0, result
        # Dry-run force message, no actual uninstall.
        assert_match(/Would force-remove: foo/, out)
        # Package still installed (nothing was removed).
        assert pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_upgrade_dry_run_does_not_install
    with_fake_tc do |tc|
      with_stubbed_externals do
        # Seed an older install on disk to make the package
        # upgradable.
        gcc_ver = FAKE_GCC_VER.to_s
        FileUtils.mkdir_p(
          tc / "gcc-#{gcc_ver}" / ARCH.name / "foo" / "0.9.0"
        )
        pkgmgr.register(FakePackage.new("foo"))

        result = nil
        out = capture_stdout {
          result = Main.main(["--upgrade", "-d"])
        }
        assert_equal 0, result
        assert_empty FakePackage.install_log
        assert_match(/Packages to upgrade.*foo/, out)
        assert_match(/Dry run/, out)
      end
    end
  end
end

# ---------------------------------------------------------------
# Tests for -a <arch> / with_target_arch in install mode.
# ---------------------------------------------------------------

class TestTargetArchScope < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_target_arch_defaults_to_ARCH
    assert_equal ARCH, pkgmgr.target_arch
  end

  def test_with_target_arch_overrides_and_restores
    x64 = ALL_ARCHS["x86_64"]
    assert_equal ARCH, pkgmgr.target_arch

    pkgmgr.with_target_arch(x64) do
      assert_equal x64, pkgmgr.target_arch
    end

    assert_equal ARCH, pkgmgr.target_arch
  end

  def test_with_target_arch_nests_correctly
    x64 = ALL_ARCHS["x86_64"]
    rv  = ALL_ARCHS["riscv64"]

    pkgmgr.with_target_arch(x64) do
      assert_equal x64, pkgmgr.target_arch

      pkgmgr.with_target_arch(rv) do
        assert_equal rv, pkgmgr.target_arch
      end

      assert_equal x64, pkgmgr.target_arch
    end

    assert_equal ARCH, pkgmgr.target_arch
  end

  def test_with_target_arch_restores_on_exception
    x64 = ALL_ARCHS["x86_64"]
    begin
      pkgmgr.with_target_arch(x64) do
        raise "boom"
      end
    rescue RuntimeError
    end
    assert_equal ARCH, pkgmgr.target_arch
  end

  def test_default_arch_reads_target_arch
    x64 = ALL_ARCHS["x86_64"]
    pkg = FakePackage.new("foo")
    assert_equal ARCH, pkg.default_arch

    pkgmgr.with_target_arch(x64) do
      assert_equal x64, pkg.default_arch
    end

    assert_equal ARCH, pkg.default_arch
  end

  def test_default_cc_reads_target_arch
    # Ensure gcc_ver is set for the test arch (read_gcc_ver_defaults
    # only runs in main(), not in tests).
    x64 = ALL_ARCHS["x86_64"]
    saved = x64.gcc_ver
    x64.gcc_ver ||= FAKE_GCC_VER
    pkg = FakePackage.new("foo")

    pkgmgr.with_target_arch(x64) do
      assert_equal x64.gcc_ver, pkg.default_cc
    end
  ensure
    x64.gcc_ver = saved
  end

  def test_arch_supported_reads_target_arch
    rv = ALL_ARCHS["riscv64"]
    x64 = ALL_ARCHS["x86_64"]
    pkg = FakePackage.new("rv_only", arch_list: [rv])

    pkgmgr.with_target_arch(rv) do
      assert pkg.arch_supported?
    end

    pkgmgr.with_target_arch(x64) do
      refute pkg.arch_supported?
    end
  end

  def test_build_dep_graph_uses_target_arch
    rv = ALL_ARCHS["riscv64"]
    pkgmgr.register(
      FakePackage.new("gcc-riscv64-musl", on_host: true, is_compiler: true)
    )
    pkgmgr.register(FakePackage.new("foo"))

    pkgmgr.with_target_arch(rv) do
      graph = pkgmgr.build_dep_graph
      assert_includes graph["foo"], "gcc-riscv64-musl"
    end
  end

  def test_build_dep_graph_default_uses_ARCH
    pkgmgr.register(
      FakePackage.new("gcc-#{ARCH.name}-musl",
                       on_host: true, is_compiler: true)
    )
    pkgmgr.register(FakePackage.new("foo"))

    graph = pkgmgr.build_dep_graph
    assert_includes graph["foo"], "gcc-#{ARCH.name}-musl"
  end
end

class TestInstallWithTargetArch < Minitest::Test
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

  def test_install_with_dash_a_for_different_arch
    # -s foo -a riscv64: installs foo for riscv64 (pkg is
    # arch-universal). The compiler dep should be riscv64's.
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(
          FakePackage.new("gcc-riscv64-musl",
                           on_host: true, is_compiler: true)
        )
        pkgmgr.register(FakePackage.new("foo"))

        out = capture_stdout {
          result = Main.main(["-s", "foo", "-a", "riscv64", "-d", "--ascii"])
          assert_equal 0, result
        }
        # Plan should include the riscv64 compiler as a dep.
        assert_match(/gcc-riscv64-musl/, out)
        assert_match(/^foo$/, out)
      end
    end
  end

  def test_install_with_dash_a_refuses_unsupported_arch
    # -s rv_only -a x86_64: rv_only supports only riscv64.
    # Should error.
    with_fake_tc do
      with_stubbed_externals do
        rv = ALL_ARCHS["riscv64"]
        pkgmgr.register(FakePackage.new("rv_only", arch_list: [rv]))

        out = capture_stdout {
          result = Main.main(["-s", "rv_only", "-a", "x86_64"])
          assert_equal 1, result
        }
      end
    end
  end

  def test_install_ALL_with_dash_a
    # -s ALL -a riscv64: expand against riscv64's installable set.
    with_fake_tc do
      with_stubbed_externals do
        rv = ALL_ARCHS["riscv64"]
        i3 = ALL_ARCHS["i386"]
        pkgmgr.register(
          FakePackage.new("gcc-riscv64-musl",
                           on_host: true, is_compiler: true)
        )
        pkgmgr.register(FakePackage.new("rv_pkg", arch_list: [rv]))
        pkgmgr.register(FakePackage.new("i3_pkg", arch_list: [i3]))
        pkgmgr.register(FakePackage.new("universal"))

        out = capture_stdout {
          result = Main.main(["-s", "ALL", "-a", "riscv64", "-d", "--ascii"])
          assert_equal 0, result
        }
        # rv_pkg and universal in the plan; i3_pkg excluded.
        assert_match(/rv_pkg/, out)
        assert_match(/universal/, out)
        refute_match(/i3_pkg/, out)
      end
    end
  end

  def test_install_with_dash_a_ALL_iterates_archs
    # -s foo -a ALL: install foo for each supported arch.
    with_fake_tc do
      with_stubbed_externals do
        ALL_ARCHS.values.each do |a|
          pkgmgr.register(
            FakePackage.new("gcc-#{a.name}-musl",
                             on_host: true, is_compiler: true)
          )
        end
        pkgmgr.register(FakePackage.new("foo"))

        out = capture_stdout {
          result = Main.main(["-s", "foo", "-a", "ALL", "-d", "--ascii"])
          assert_equal 0, result
        }
        ALL_ARCHS.values.each do |a|
          assert_match(/Architecture: #{a.name}/, out)
        end
      end
    end
  end

  def test_install_with_dash_a_ALL_skips_unsupported_archs
    with_fake_tc do
      with_stubbed_externals do
        rv = ALL_ARCHS["riscv64"]
        ALL_ARCHS.values.each do |a|
          pkgmgr.register(
            FakePackage.new("gcc-#{a.name}-musl",
                             on_host: true, is_compiler: true)
          )
        end
        pkgmgr.register(FakePackage.new("rv_only", arch_list: [rv]))

        out = capture_stdout {
          result = Main.main(["-s", "rv_only", "-a", "ALL", "-d", "--ascii"])
          assert_equal 0, result
        }
        assert_match(/Architecture: riscv64/, out)
        assert_match(/^rv_only$/, out)
        assert_match(/Skipping rv_only: not supported on i386/, out)
      end
    end
  end

  def test_compiler_dep_matches_target_arch
    # When installing for riscv64, the implicit dep must be
    # gcc-riscv64-musl, not gcc-<default_ARCH>-musl.
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(
          FakePackage.new("gcc-riscv64-musl",
                           on_host: true, is_compiler: true)
        )
        pkgmgr.register(FakePackage.new("foo"))

        out = capture_stdout {
          result = Main.main(["-s", "foo", "-a", "riscv64", "-d", "--ascii"])
          assert_equal 0, result
        }
        # In ASCII tree: foo has gcc-riscv64-musl as child.
        assert_match(/^foo$/, out)
        assert_match(/^  gcc-riscv64-musl$/, out)
      end
    end
  end
end
