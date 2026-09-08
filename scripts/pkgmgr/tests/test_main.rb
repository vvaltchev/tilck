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

# -h fits the terminal: 80 columns when stdout is not one, else its
# width up to 120, the descriptions re-flowed beside their switches.
class TestHelpFitsTheTerminal < Minitest::Test

  include TestHelper

  def help_lines(columns)
    out = StringIO.new
    old = $stdout
    $stdout = out
    Term.stub(:columns, columns) { Main.parse_options(["-h"]) }
    return out.string.lines.map(&:chomp)
  ensure
    $stdout = old
  end

  def test_eighty_columns_when_stdout_is_not_a_terminal
    lines = help_lines(80)
    assert lines.length > 100, "the help lost its options"
    assert_equal 80, lines.map(&:length).max
    assert lines.any? { |l| l.start_with?("    -l, --list") }
    # the description column is one column, every continuation on it
    col = lines.find { |l| l =~ /\A\s+-l, --list\s+\S/ }.index("List")
    conts = lines.select { |l| l.start_with?(" " * col) && l[col] != " " }
    assert conts.length > 50
  end

  def test_a_wider_terminal_gets_wider_lines_up_to_the_cap
    wide = help_lines(100)
    assert_equal 100, wide.map(&:length).max
    assert wide.length < help_lines(80).length, "not re-flowed"
  end

  # A description line beginning with a dash is read by OptionParser
  # as another switch of the option -- "-d to see what would go" made
  # -d an argument-taking alias, and "-a <arch> for cross-arch queries"
  # gave -D an argument spec of prose. No switch may carry one.
  def test_no_switch_was_made_out_of_a_description
    Main::PARSER.top.list.each { |sw|
      next if !sw.respond_to?(:arg)
      refute_match(/\s\S+\s/, sw.arg.to_s,
                   "#{sw.long.first || sw.short.first}: #{sw.arg.inspect}")
    }
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

  # ALL is "everything installABLE", which is a question about a
  # world: what is registered, and what is already there. These asked
  # it of the developer's toolchain -- so the set they expanded to
  # depended on what that machine had built, and the assertions held
  # by luck. with_fake_tc gives them a world with nothing in it.

  def test_install_ALL_expands_to_installable_non_compilers
    with_fake_tc do
      pkgmgr.register(FakePackage.new("foo"))
      pkgmgr.register(FakePackage.new("bar"))
      pkgmgr.register(
        FakePackage.new("gcc-fake-musl", on_host: true, is_compiler: true)
      )

      result = Main.expand_install_all(["ALL"])
      names = result.map { |s| s.split(":").first }.sort
      assert_equal ["bar", "foo"], names
    end
  end

  def test_install_ALL_skips_packages_not_supported_on_current_arch
    with_fake_tc do
      other_arch = (ALL_ARCHS.values - [ARCH]).first
      pkgmgr.register(FakePackage.new("universal"))
      pkgmgr.register(FakePackage.new("other_only", arch_list: [other_arch]))

      result = Main.expand_install_all(["ALL"])
      names = result.map { |s| s.split(":").first }
      assert_includes names, "universal"
      refute_includes names, "other_only"
    end
  end

  def test_install_ALL_coexists_with_named_packages
    with_fake_tc do
      pkgmgr.register(FakePackage.new("foo"))
      pkgmgr.register(FakePackage.new("bar"))

      result = Main.expand_install_all(["custom", "ALL"])
      names = result.map { |s| s.split(":").first }
      assert_includes names, "custom"
      assert_includes names, "foo"
      assert_includes names, "bar"
    end
  end

  def test_install_ALL_respects_target_arch_scope
    # When with_target_arch scopes to riscv64, a package that only
    # supports riscv64 is included; an i386-only package is excluded.
    with_fake_tc do
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

        output = run_cli("-l").last
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

        output = run_cli("-l", "-g", "arch").last
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

        output = run_cli("-l", "-c", "ALL").last
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
        result = run_cli("-j").first
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

        result = run_cli("--check-for-updates").first
        assert_equal 0, result
      end
    end
  end

  def test_check_for_updates_with_upgrade_needed
    with_fake_tc do |tc|
      with_stubbed_externals do
        gcc_ver = FAKE_GCC_VER.to_s
        old_dir = target_pkgs(ARCH, gcc_ver) / "foo" / "0.9.0"
        FileUtils.mkdir_p(old_dir)

        pkgmgr.register(FakePackage.new("foo"))

        result = run_cli("--check-for-updates").first
        assert_equal 2, result
      end
    end
  end

  def test_install_single_package
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))

        result = run_cli("-s", "foo").first
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

        result = run_cli("-s", "a").first
        assert_equal 0, result
        assert_equal ["b", "a"], FakePackage.install_log
      end
    end
  end

  def test_install_unknown_package
    with_fake_tc do
      with_stubbed_externals do
        result = run_cli("-s", "nonexistent").first
        assert_equal 1, result
      end
    end
  end

  def test_upgrade_mode
    with_fake_tc do |tc|
      with_stubbed_externals do
        gcc_ver = FAKE_GCC_VER.to_s
        old_dir = target_pkgs(ARCH, gcc_ver) / "foo" / "0.9.0"
        FileUtils.mkdir_p(old_dir)
        pkgmgr.register(FakePackage.new("foo"))

        result = run_cli("--upgrade").first
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

        result = run_cli("--upgrade").first
        assert_equal 0, result
        assert_empty FakePackage.install_log
      end
    end
  end

  # The no-mode run installs the Tilck stack of this target: the
  # meta-package, and with it what is declared default, as its
  # dependencies.
  def test_default_install_mode
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("dflt", default: true))
        pkgmgr.register(FakePackage.new("opt"))
        stack = register_tilck_stack!

        result = run_cli().first
        assert_equal 0, result
        assert_includes FakePackage.install_log, "dflt"
        refute_includes FakePackage.install_log, "opt"
        assert stack.installed?(stack.default_ver), "the stack itself"
      end
    end
  end

  # A target without a Tilck stack gets nothing installed by default,
  # and is told so.
  def test_default_install_without_a_stack_installs_nothing
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("dflt", default: true))
        result, out = run_cli()
        assert_equal 0, result
        assert_match(/No Tilck stack is defined for i386\/pc/, out)
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_default_install_plan_shows_tree_not_linear_order
    # Regression: the default-install branch (no mode flag) used to
    # print "Install order: a -> b" instead of the dependency tree.
    # It should now use the same tree renderer as -s install plans.
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("dflt_root",
          default: true,
          dep_list: [Dep("dflt_dep", false)]))
        pkgmgr.register(FakePackage.new("dflt_dep", default: true))
        register_tilck_stack!

        result, out = run_cli("--ascii")

        assert_equal 0, result
        assert_match(/Install plan:/, out)
        # ASCII tree: the stack is the root, dflt_root its child,
        # dflt_dep the child's child.
        assert_match(/^tilck-i386-pc$/, out)
        assert_match(/^  dflt_root$/, out)
        assert_match(/^    dflt_dep$/, out)
        # Old linear format must be gone.
        refute_match(/Install order:/, out)
      end
    end
  end

  def test_config_non_configurable
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))

        result = run_cli("-C", "foo").first
        assert_equal 1, result
      end
    end
  end

  def test_config_unknown_package
    with_fake_tc do
      with_stubbed_externals do
        result = run_cli("-C", "nonexistent").first
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
        result, out = run_cli("-s", "foo", "-d", "--ascii")
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
        result, out = run_cli("-s", "a", "-d", "--ascii")
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
        result, out = run_cli("-s", "foo", "-f", "-d")
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
        old_dir = target_pkgs(ARCH, gcc_ver) / "foo" / "0.9.0"
        FileUtils.mkdir_p(old_dir)
        pkgmgr.register(FakePackage.new("foo"))

        result = nil
        result, out = run_cli("--upgrade", "-d")
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

        result, out = run_cli("-s", "foo", "-a", "riscv64", "-d", "--ascii")
        assert_equal 0, result
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

        result, _out = run_cli("-s", "rv_only", "-a", "x86_64")
        assert_equal 1, result
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

        result, out = run_cli("-s", "ALL", "-a", "riscv64", "-d", "--ascii")
        assert_equal 0, result
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

        result, out = run_cli("-s", "foo", "-a", "ALL", "-d", "--ascii")
        assert_equal 0, result
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

        result, out = run_cli("-s", "rv_only", "-a", "ALL", "-d", "--ascii")
        assert_equal 0, result
        assert_match(/Architecture: riscv64/, out)
        assert_match(/^rv_only$/, out)
        assert_match(/Skipping rv_only: not supported on arch i386/, out)
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

        result, out = run_cli("-s", "foo", "-a", "riscv64", "-d", "--ascii")
        assert_equal 0, result
        # In ASCII tree: foo has gcc-riscv64-musl as child.
        assert_match(/^foo$/, out)
        assert_match(/^  gcc-riscv64-musl$/, out)
      end
    end
  end
end

# ---------------------------------------------------------------
# Tests for the fancy (non-ASCII) box-drawing dep tree renderer.
# Unlike the other Main tests, these call render_dep_trees
# directly with a hand-built graph — no FakePackage, no pkgmgr
# setup — so they exercise the rendering logic in isolation.
# ---------------------------------------------------------------

class TestRenderDepTreesFancy < Minitest::Test

  def render(roots, graph, installed: [], show_installed: false)
    Main.render_dep_trees(roots, graph,
                          installed: Set.new(installed),
                          show_installed: show_installed,
                          ascii: false)
  end

  def test_leaf_root_uses_dash_bullet_not_corner
    # A root with no deps should render with "─ name" (bullet),
    # not "┌ name" — there is no subtree trunk to open.
    out = render(["foo"], {"foo" => []})
    assert_equal ["    ─ foo"], out
  end

  def test_root_with_all_deps_installed_uses_dash_bullet
    # In install-plan mode (show_installed: false), already-installed
    # deps are filtered out. If ALL of a root's deps are installed,
    # the root becomes effectively a leaf → "─ name".
    out = render(["foo"],
                 {"foo" => ["bar"], "bar" => []},
                 installed: ["bar"])
    assert_equal ["    ─ foo"], out
  end

  def test_root_with_unmet_deps_uses_corner
    # A root that has visible deps should still use "┌ name".
    out = render(["foo"], {"foo" => ["bar"], "bar" => []})
    assert_equal [
      "    ┌ foo",
      "    │",
      "    └── bar",
    ], out
  end

  def test_leaf_root_in_deps_mode_shows_no_dependencies_line
    # In --deps mode (show_installed: true), a genuine leaf root
    # still gets the "─" bullet and the "(no dependencies)" hint.
    out = render(["foo"], {"foo" => []}, show_installed: true)
    assert_equal [
      "    ─ foo",
      "    (no dependencies)",
    ], out
  end

  def test_grandchild_gets_pre_first_child_trunk_spacer
    # Between a parent with children and its first child, there
    # should be an extra "...│" line showing the inner trunk —
    # so the vertical connector is visible even with one child.
    out = render(["foo"],
                 {"foo" => ["bar"], "bar" => ["baz"], "baz" => []})
    assert_equal [
      "    ┌ foo",
      "    │",
      "    └── bar",
      "        │",
      "        └── baz",
    ], out
  end

  def test_multiple_roots_separated_by_blank_line
    # Mixed: one leaf root + one subtree root. Two trees are
    # separated by a blank line.
    out = render(["a", "b"],
                 {"a" => [], "b" => ["c"], "c" => []})
    assert_equal [
      "    ─ a",
      "",
      "    ┌ b",
      "    │",
      "    └── c",
    ], out
  end
end

# ---------------------------------------------------------------
# A subtree is drawn once. The graphs are diamonds all the way down,
# and a tree that redraws a shared subtree at every mention grows
# exponentially in its depth: twenty thousand lines for host_qemu's
# fifty packages. Later mentions say "(+ deps)" and point back up.
# ---------------------------------------------------------------

class TestRenderDepTreesOnce < Minitest::Test

  # a needs b and c; both need d; d needs e. Two subtrees are shared:
  # d (with something under it) and, through it, e.
  DIAMOND = {
    "a" => ["b", "c"], "b" => ["d"], "c" => ["d"], "d" => ["e"], "e" => []
  }.freeze

  def fancy(roots, graph, **kw)
    Main.render_dep_trees(roots, graph, ascii: false, **kw)
  end

  def ascii(roots, graph, **kw)
    Main.render_dep_trees(roots, graph, ascii: true, **kw)
  end

  def test_the_second_mention_is_marked_and_not_redrawn
    assert_equal [
      "    ┌ a",
      "    │",
      "    ├── b",
      "    │   │",
      "    │   └── d",
      "    │       │",
      "    │       └── e",
      "    │",
      "    └── c",
      "        │",
      "        └── d (+ deps)",
    ], fancy(["a"], DIAMOND).map { |l| l.gsub(/\e\[[0-9;]*m/, "") }
  end

  def test_the_mark_is_dim_in_the_fancy_mode
    line = fancy(["a"], DIAMOND).last
    assert_equal "        └── d#{Term::DIM} (+ deps)#{Term::RESET}", line
  end

  def test_a_shared_leaf_is_never_marked
    # Nothing is left out under a leaf, so there is nothing to point
    # back at: the name is repeated bare, every time.
    out = fancy(["a"], {"a" => ["b", "c"], "b" => ["e"], "c" => ["e"],
                        "e" => []})
    assert_equal 2, out.count { |l| l.end_with?("── e") }
    assert_empty out.grep(/\+ deps/)
  end

  def test_a_root_already_drawn_under_an_earlier_root_is_marked
    out = fancy(["a", "d"], DIAMOND).map { |l| l.gsub(/\e\[[0-9;]*m/, "") }
    assert_equal "    ─ d (+ deps)", out.last
    refute_includes out, "    (no dependencies)"
  end

  def test_ascii_mode_follows_the_same_rule
    assert_equal ["a", "  b", "    d", "      e", "  c", "    d (+ deps)"],
                 ascii(["a"], DIAMOND)
  end

  def test_installed_deps_hidden_by_the_plan_do_not_count_as_drawn
    # In plan mode d is installed and hidden; b and c become leaves,
    # and a leaf is never marked.
    out = fancy(["a"], DIAMOND, installed: Set.new(["d"]))
    assert_empty out.grep(/\+ deps/)
  end

  def test_a_cycle_ends_at_its_first_repeat
    out = ascii(["a"], {"a" => ["b"], "b" => ["a"]})
    assert_equal ["a", "  b", "    a (+ deps)"], out
  end
end

class TestDepTreeClosure < Minitest::Test

  DIAMOND = TestRenderDepTreesOnce::DIAMOND

  def test_each_package_once_dependencies_first
    assert_equal ["e", "d", "b", "c", "a"],
                 Main.dep_tree_closure(["a"], DIAMOND)
  end

  def test_the_plan_mode_leaves_out_what_is_installed
    assert_equal ["b", "c", "a"],
                 Main.dep_tree_closure(["a"], DIAMOND,
                                       installed: Set.new(["d"]))
  end

  def test_two_roots_share_one_closure
    assert_equal ["e", "d", "b", "c", "a"],
                 Main.dep_tree_closure(["a", "d"], DIAMOND)
  end
end

class TestRenderNameList < Minitest::Test

  def test_a_short_list_is_one_indented_line
    assert_equal ["    a, b, c"], Main.render_name_list(%w[a b c])
  end

  def test_wraps_at_eighty_columns_with_the_comma_kept_on_the_line
    names = (1..30).map { |i| "package_%02d" % i }
    out = Main.render_name_list(names)

    assert out.length > 1
    assert out.all? { |l| l.length <= 80 }, out.map(&:length).inspect
    assert out[0..-2].all? { |l| l.end_with?(",") }
    refute out.last.end_with?(",")
    assert_equal names, out.join(" ").split(/,\s*/).map(&:strip)
  end

  def test_installed_names_are_dimmed_in_deps_mode
    out = Main.render_name_list(%w[a b], installed: Set.new(["b"]),
                                show_installed: true)
    assert_equal ["    a, #{Term::DIM}b#{Term::RESET}"], out
  end

  def test_an_empty_list_is_one_empty_line
    assert_equal ["    "], Main.render_name_list([])
  end
end

class TestMainPlanShowsTheFlatList < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_install_plan_ends_with_the_packages_in_install_order
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a", dep_list: [Dep("b", false),
                                                        Dep("c", false)]))
        pkgmgr.register(FakePackage.new("b", dep_list: [Dep("d", false)]))
        pkgmgr.register(FakePackage.new("c", dep_list: [Dep("d", false)]))
        pkgmgr.register(FakePackage.new("d", dep_list: [Dep("e", false)]))
        pkgmgr.register(FakePackage.new("e"))

        result, out = run_cli("-s", "a", "-d", "--ascii")
        assert_equal 0, result

        assert_match(/^    d \(\+ deps\)$/, out)
        assert_equal 1, out.scan(/^      e$/).length

        assert_match(/5 package\(s\) to install, in this order:/, out)
        list = out[/in this order:\n(.*)$/, 1]
        names = list.strip.split(", ")
        assert_equal 5, names.length
        assert_operator names.index("e"), :<, names.index("d")
        assert_operator names.index("d"), :<, names.index("b")
        assert_equal "a", names.last
      end
    end
  end

  def test_deps_mode_ends_with_the_whole_closure
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a", dep_list: [Dep("b", false)]))
        pkgmgr.register(FakePackage.new("b", dep_list: [Dep("c", false)]))
        pkgmgr.register(FakePackage.new("c"))

        result, out = run_cli("--deps", "a", "--ascii")
        assert_equal 0, result
        assert_match(/3 package\(s\) in all, dependencies first:/, out)
        assert_match(/^    c, b, a$/, out)
      end
    end
  end
end

# The same rule, in the implementation, at the door where the package
# name is resolved: a version is one the package declares, exactly or
# by a series that picks one, or the request is refused with the list.
class TestMainResolvesVersions < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def choosy
    q = FakePackage.new("choosy")
    q.define_singleton_method(:installable_versions) {
      [Ver("6.2.0"), Ver("7.2.0")]
    }
    q
  end

  def test_a_series_installs_the_one_release_it_has
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(choosy)
        rc, out = run_cli("-s", "choosy:6", "-d", "--ascii")
        assert_equal 0, rc, out
        assert_match(/^choosy$/, out)
        refute_match(/not a version/, out)
      end
    end
  end

  def test_a_version_nobody_offers_is_refused_with_the_list
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(choosy)
        rc, out = run_cli("-s", "choosy:9.9.9", "-d")
        assert_equal 1, rc
        assert_match(/choosy:9.9.9 is not a version choosy can install/, out)
        assert_match(/Available: 6.2.0, 7.2.0/, out)
        refute_match(/Install plan/, out)
      end
    end
  end

  def test_a_package_declaring_nothing_takes_the_version_as_written
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("plain"))
        rc, out = run_cli("-s", "plain:3.3.3")
        assert_equal 0, rc, out
        assert_match(/Install plain version: 3.3.3/, out)
      end
    end
  end

  def test_uninstall_resolves_the_same_way
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(choosy)
        rc, out = run_cli("-u", "choosy:9.9.9")
        assert_equal 1, rc
        assert_match(/not a version choosy can install/, out)
      end
    end
  end
end

# --rebuild: the installs --check-for-updates lists as NEEDS_REBUILD,
# rebuilt where they are, at their version, as they were asked for.
# --upgrade is the remedy for a bumped version and does not touch
# these; running it and being told "up to date" while fifteen installs
# read changed is how this mode came to exist.
class TestMainRebuild < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # An install, then a recipe edit under it: the record reads changed.
  def install_then_change(pkg)
    rc, _ = run_cli("-s", pkg.name)
    assert_equal 0, rc
    pkg.define_singleton_method(:build_flags) { |v = nil| ["--changed"] }
    pkgmgr.refresh
    inst = pkg.find_install(pkg.default_ver)
    assert_equal :changed, pkg.build_inputs_state_of(inst)
    FakePackage.clear_log!
    return inst
  end

  def test_nothing_stale_rebuilds_nothing
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        run_cli("-s", "foo")
        FakePackage.clear_log!
        rc, out = run_cli("--rebuild")
        assert_equal 0, rc
        assert_match(/built from the sources we have/, out)
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_a_changed_install_is_rebuilt_where_it_is
    with_fake_tc do
      with_stubbed_externals do
        foo = FakePackage.new("foo")
        pkgmgr.register(foo)
        before = install_then_change(foo)

        rc, out = run_cli("--rebuild")
        assert_equal 0, rc, out
        assert_match(/foo:#{Regexp.escape(before.ver.to_s)} at /, out)
        assert_equal ["foo"], FakePackage.install_log

        pkgmgr.refresh
        after = foo.find_install(foo.default_ver)
        assert_equal :ok, foo.build_inputs_state_of(after)
        assert_equal before.coords, after.coords
        assert_equal before.ver, after.ver
        assert after.default_install, "a default install came back pinned"
      end
    end
  end

  def test_dry_run_lists_and_touches_nothing
    with_fake_tc do
      with_stubbed_externals do
        foo = FakePackage.new("foo")
        pkgmgr.register(foo)
        install_then_change(foo)

        rc, out = run_cli("--rebuild", "-d")
        assert_equal 0, rc
        assert_match(/Installs to rebuild/, out)
        assert_match(/nothing rebuilt/, out)
        assert_empty FakePackage.install_log
        inst = foo.find_install(foo.default_ver)
        assert_equal :changed, foo.build_inputs_state_of(inst)
      end
    end
  end

  def test_a_bumped_version_is_left_to_upgrade
    with_fake_tc do
      with_stubbed_externals do
        foo = FakePackage.new("foo")
        pkgmgr.register(foo)
        install_then_change(foo)
        # ...and its default moves on: now it is an upgrade, not a rebuild.
        foo.define_singleton_method(:default_ver) { Ver("2.0.0") }
        pkgmgr.refresh

        rc, out = run_cli("--rebuild")
        assert_equal 0, rc
        assert_match(/built from the sources we have/, out)
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_dependencies_are_rebuilt_first
    with_fake_tc do
      with_stubbed_externals do
        base = FakePackage.new("base")
        top = FakePackage.new("top", dep_list: [Dep("base", false)])
        pkgmgr.register(base)
        pkgmgr.register(top)
        run_cli("-s", "top")
        [top, base].each { |p|
          p.define_singleton_method(:build_flags) { |v = nil| ["--changed"] }
        }
        pkgmgr.refresh
        FakePackage.clear_log!

        rc, _ = run_cli("--rebuild")
        assert_equal 0, rc
        assert_equal ["base", "top"], FakePackage.install_log
      end
    end
  end
end

# What an install was built against is recorded beside it, and a
# rebuild builds against the same: mpfr asked alone answers gmp's
# default, while the gcc that pulled it in pinned another, and that
# resolution is gone the moment the request is done.
# A rebuild that does not finish leaves the old install where it was.
# The first real --rebuild removed first and built second, and left
# no isl and then no GCC.
class TestMainRebuildKeepsTheOldTreeOnFailure < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # Builds once, and never again.
  class Once < TestHelper::FakePackage
    def install_impl_internal(install_dir)
      @built = (@built || 0) + 1
      return false if @built > 1
      super
    end
  end

  def test_the_old_install_survives_a_failed_rebuild
    with_fake_tc do
      with_stubbed_externals do
        pkg = Once.new("once")
        pkgmgr.register(pkg)
        assert_equal 0, run_cli("-s", "once").first
        pkg.define_singleton_method(:build_flags) { |v = nil| ["--changed"] }
        pkgmgr.refresh
        before = pkg.find_install(pkg.default_ver)
        assert_equal :changed, pkg.build_inputs_state_of(before)

        rc, out = run_cli("--rebuild", laws: false,
                          because: "a build that fails is outside the " \
                                   "model, which has no failing builds")
        assert_equal 1, rc
        assert_match(/Could not rebuild: once/, out)

        pkgmgr.refresh
        after = pkg.find_install(pkg.default_ver)
        refute_nil after, "the old install is gone"
        assert_equal before.path, after.path
        assert_equal :changed, pkg.build_inputs_state_of(after),
                     "the old install came back as something else"
        refute (TC_STAGING / "replaced" / "once").exist?,
               "the tree set aside was left under staging"
      end
    end
  end
end

# ...and when it raises rather than returns false, which is how a
# recipe reports a dependency it cannot find.
class TestMainRebuildKeepsTheOldTreeOnARaise < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  class Raising < TestHelper::FakePackage
    def install_impl_internal(install_dir)
      @built = (@built || 0) + 1
      raise "host_nothing version 1.0.0 is not installed" if @built > 1
      super
    end
  end

  def test_the_old_install_survives_and_the_run_ends_cleanly
    with_fake_tc do
      with_stubbed_externals do
        pkg = Raising.new("raisy")
        pkgmgr.register(pkg)
        assert_equal 0, run_cli("-s", "raisy").first
        pkg.define_singleton_method(:build_flags) { |v = nil| ["--changed"] }
        pkgmgr.refresh
        before = pkg.find_install(pkg.default_ver)

        rc, out = run_cli("--rebuild", laws: false,
                          because: "a build that raises is outside the " \
                                   "model, which has no failing builds")
        assert_equal 1, rc
        assert_match(/host_nothing version 1.0.0 is not installed/, out)
        assert_match(/Could not rebuild: raisy/, out)

        pkgmgr.refresh
        after = pkg.find_install(pkg.default_ver)
        refute_nil after, "the old install is gone"
        assert_equal before.path, after.path
        refute (TC_STAGING / "replaced").exist?,
               "the tree set aside was left under staging"
      end
    end
  end
end

class TestMainRebuildBuildsAgainstTheSame < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # A dependency with two versions, a user of it that notes which one
  # it saw, and a root that pins the older.
  class Noting < TestHelper::FakePackage
    attr_reader :saw
    def install_impl_internal(install_dir)
      @saw ||= []
      @saw << pkgmgr.resolved_ver("host_gmp")
      super
    end
  end

  # Host packages, because only a host dependency can be pinned.
  HOST = { on_host: true, host_tier: :distro,
           arch_list: ALL_HOST_ARCHS.values }.freeze

  def world
    gmp = FakePackage.new("host_gmp", **HOST)
    gmp.define_singleton_method(:default_ver) { Ver("2.0.0") }
    gmp.define_singleton_method(:installable_versions) {
      [Ver("1.0.0"), Ver("2.0.0")]
    }
    user = Noting.new("host_user", dep_list: [Dep("host_gmp", true)], **HOST)
    root = FakePackage.new("host_root", **HOST,
                           dep_list: [Dep("host_user", true),
                                      Dep("host_gmp", true, ver: Ver("1.0.0"))])
    [gmp, user, root].each { |p| pkgmgr.register(p) }
    return [gmp, user, root]
  end

  def record_of(pkg)
    return pkg.find_install(pkg.default_ver).path / InstallDeps::FILE
  end

  def make_stale(pkg)
    pkg.define_singleton_method(:build_flags) { |v = nil| ["--changed"] }
    pkgmgr.refresh
  end

  def test_the_record_names_the_version_the_request_resolved
    with_fake_tc do
      with_stubbed_externals do
        gmp, user, = world
        assert_equal 0, run_cli("-s", "host_root").first
        inst = user.find_install(user.default_ver)
        assert_equal({ "host_gmp" => Ver("1.0.0") },
                     InstallDeps.read(inst.path))
        assert_equal [Ver("1.0.0")], user.saw
      end
    end
  end

  def test_a_rebuild_builds_against_what_the_record_says
    with_fake_tc do
      with_stubbed_externals do
        gmp, user, = world
        run_cli("-s", "host_root")
        make_stale(user)
        FakePackage.clear_log!

        rc, out = run_cli("--rebuild")
        assert_equal 0, rc, out
        assert_equal ["host_user"], FakePackage.install_log
        assert_equal [Ver("1.0.0"), Ver("1.0.0")], user.saw,
                     "the rebuild resolved gmp to something else"
      end
    end
  end

  def test_without_a_record_the_one_installed_version_is_taken
    with_fake_tc do
      with_stubbed_externals do
        gmp, user, = world
        run_cli("-s", "host_root")
        File.delete(record_of(user))
        make_stale(user)

        rc, out = run_cli("--rebuild")
        assert_equal 0, rc, out
        assert_equal Ver("1.0.0"), user.saw.last, "gmp 1.0.0 is the only one"
      end
    end
  end

  def test_without_a_record_and_two_installed_it_refuses_before_removing
    with_fake_tc do
      with_stubbed_externals do
        gmp, user, = world
        run_cli("-s", "host_root")
        run_cli("-s", "host_gmp:2.0.0")
        File.delete(record_of(user))
        make_stale(user)
        FakePackage.clear_log!

        # The refusal reads a per-install record the model does not
        # carry; the model would rebuild, and rightly says so.
        rc, out = run_cli("--rebuild", laws: false,
                          because: "the .built_against record is not " \
                                   "part of the model's world")
        assert_equal 1, rc
        assert_match(/no record of which host_gmp it was built against/, out)
        assert_empty FakePackage.install_log
        refute_nil user.find_install(user.default_ver), "old install removed"
      end
    end
  end
end

# The order of -l: Tilck's packages, then the host side -- the tools
# the system compiler built, the stacks (every one with a count,
# because the listing shows only the current stack's packages), and
# the current stack's packages last.
class TestMainListOrder < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_tilck_then_host_tools_then_stacks_then_the_current_stack
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("target_pkg"))
        pkgmgr.register(FakePackage.new("host_tool", on_host: true,
                                        arch_list: ALL_HOST_ARCHS.values))
        pkgmgr.register(FakePackage.new("host_thing", on_host: true,
                                        host_tier: :stack,
                                        arch_list: ALL_HOST_ARCHS.values))
        run_cli("-s", "target_pkg")
        run_cli("-s", "host_tool")
        run_cli("-s", "host_thing")

        rc, out = run_cli("-l")
        assert_equal 0, rc

        at = ->(text) { out.index(text) || flunk("#{text.inspect} missing") }
        tilck  = at.call("Tilck packages built by GCC")
        tools  = at.call("Host packages built by system CC")
        stacks = out.index("Host stacks") || at.call("No host stacks")
        stack  = at.call("Host packages built by GCC")

        assert tilck < tools && tools < stacks && stacks < stack,
               "order was #{[tilck, tools, stacks, stack].inspect}"
      end
    end
  end
end

# A rebuild plans the install as it was made: a dependency the recipe
# has grown since is put in first. QEMU learned libslirp after four of
# it had been built, and the first --rebuild died asking for it.
class TestMainRebuildPlansItsDependencies < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_a_dependency_grown_since_the_install_is_installed_first
    with_fake_tc do
      with_stubbed_externals do
        top = FakePackage.new("top")
        pkgmgr.register(top)
        run_cli("-s", "top")

        # The recipe grows a dependency, which is also a change.
        base = FakePackage.new("base")
        pkgmgr.register(base)
        top.define_singleton_method(:dep_list) { [Dep("base", false)] }
        top.define_singleton_method(:build_flags) { |v = nil| ["--changed"] }
        pkgmgr.refresh
        assert_nil base.find_install(base.default_ver)
        FakePackage.clear_log!

        rc, out = run_cli("--rebuild")
        assert_equal 0, rc, out
        assert_equal ["base", "top"], FakePackage.install_log
        refute_nil base.find_install(base.default_ver)
      end
    end
  end
end
