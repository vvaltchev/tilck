# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'
require_relative '../system_pkgs'
require_relative '../system_deps'
require_relative '../gdk_pixbuf'

#
# Test doubles for the outside world.
#
# SystemDeps::Env is the only thing in system_deps.rb that runs
# commands, reads PATH or talks to a terminal, so replacing it makes
# every decision below testable without a package manager, a network
# or a tty.
#

class FakeSysBackend

  attr_reader :id, :queried

  def initialize(id: :apt, installed: [])
    @id = id
    @installed = installed
    @queried = []
  end

  def name = @id.to_s

  def installed?(pkg)
    @queried << pkg
    return @installed.include?(pkg)
  end

  def full_install_argv(pkgs, assume_yes: false)
    return ["fakepm", "install", *(assume_yes ? ["-y"] : []), *pkgs]
  end
end

class FakeSysEnv

  attr_accessor :tools, :backend, :answers, :interactive, :ci, :flags,
                :run_result, :on_run
  attr_reader :ran, :asked

  # tools: { "rustc" => { path: "/usr/bin/rustc", ver: "1.66.1" } }
  #        a nil :ver means the binary is there but won't say what it is
  def initialize(tools: {}, backend: nil, answers: [], interactive: true,
                 ci: false, flags: {}, run_result: true)
    @tools = tools
    @backend = backend
    @answers = answers
    @interactive = interactive
    @ci = ci
    @flags = flags
    @run_result = run_result
    @ran = []
    @asked = []
    @on_run = nil
  end

  def which(cmd)
    t = @tools[cmd]
    return t ? t[:path] : nil
  end

  def probe_version(path, flag, re)
    t = @tools.values.find { |v| v[:path] == path }
    return nil if t.nil? || t[:ver].nil?
    return SafeVer(t[:ver])
  end

  def run(argv)
    @ran << argv
    @on_run&.call(argv, self)
    return @run_result
  end

  def ask(q, default: true)
    @asked << q
    return @answers.empty? ? default : @answers.shift
  end

  def interactive? = @interactive
  def in_ci? = @ci
  def env_flag(name) = !!@flags[name]
end

class FakeSysPkg

  attr_reader :name

  def initialize(name, deps)
    @name = name
    @deps = deps
  end

  def system_deps(ver = nil) = @deps
end

class FakeSysPm

  def initialize(map) = @map = map
  def get(name) = @map[name]
end

module SysDepCapture

  # Run a block with stdout captured, returning both what it printed
  # and what it returned -- the messages are as much of the contract
  # as the return value is.
  def capture_out(&block)
    saved = $stdout
    $stdout = StringIO.new
    result = block.call
    return { text: $stdout.string, result: result }
  ensure
    $stdout = saved
  end
end

# Shorthands used all over the tests below.
def rustc_dep(min = "1.85")
  return SystemDeps::SysDep.new(
    key: :rustc, what: "the Rust compiler", command: "rustc",
    min_ver: Ver(min), installer: SystemDeps::RUSTUP
  )
end

def ssl_dep
  return SystemDeps::SysDep.new(
    key: :ssl, what: "OpenSSL headers",
    pkgs: { apt: "libssl-dev", dnf: "openssl-devel" }
  )
end


class TestSystemPkgsBackends < Minitest::Test

  include TestHelper

  def test_apt_install_argv
    b = SystemPkgs::AptBackend.new
    assert_equal ["apt", "install", "a", "b"],
                 b.install_argv(["a", "b"])
    assert_equal ["apt", "install", "-y", "a"],
                 b.install_argv(["a"], assume_yes: true)
  end

  def test_dnf_install_argv
    b = SystemPkgs::DnfBackend.new
    assert_equal ["dnf", "install", "x"], b.install_argv(["x"])
    assert_equal ["dnf", "install", "-y", "x"],
                 b.install_argv(["x"], assume_yes: true)
    assert_equal ["rpm", "-q", "x"], b.query_argv("x")
  end

  def test_pacman_install_argv
    b = SystemPkgs::PacmanBackend.new
    assert_equal ["pacman", "-S", "--needed", "x"], b.install_argv(["x"])
    assert_equal ["pacman", "-S", "--needed", "--noconfirm", "x"],
                 b.install_argv(["x"], assume_yes: true)
  end

  def test_freebsd_pkg_install_argv
    b = SystemPkgs::PkgBackend.new
    assert_equal ["pkg", "install", "x"], b.install_argv(["x"])
    assert_equal ["pkg", "info", "-e", "x"], b.query_argv("x")
  end

  def test_brew_needs_no_root
    b = SystemPkgs::BrewBackend.new
    assert_equal false, b.needs_root?
    assert_equal ["brew", "install", "x"],
                 b.full_install_argv(["x"])
  end

  # dpkg's own exit code, and the "grep for Status" idiom the bash
  # bootstrap uses, both call a removed-but-not-purged package
  # installed. It exits 0 and prints "Status: deinstall ok
  # config-files" while none of its files are on disk.
  def test_apt_installed_distinguishes_config_files_from_installed
    b = SystemPkgs::AptBackend.new

    SystemPkgs.stub(:run_capture, [true, "installed"]) do
      assert_equal true, b.installed?("bash")
    end

    SystemPkgs.stub(:run_capture, [true, "config-files"]) do
      assert_equal false, b.installed?("removed-pkg")
    end

    SystemPkgs.stub(:run_capture, [false, ""]) do
      assert_equal false, b.installed?("never-heard-of-it")
    end
  end
end


class TestSystemPkgsDetect < Minitest::Test

  include TestHelper

  def test_detect_by_distro_id
    assert_equal :apt, SystemPkgs.detect("linux", ids: ["debian"]).id
    assert_equal :apt, SystemPkgs.detect("linux", ids: ["ubuntu"]).id
    assert_equal :dnf, SystemPkgs.detect("linux", ids: ["fedora"]).id
    assert_equal :pacman, SystemPkgs.detect("linux", ids: ["arch"]).id
  end

  # The lists only name family roots; derivatives are reached through
  # ID_LIKE, which is why Mint and Rocky need no entries of their own.
  def test_detect_by_id_like
    b = SystemPkgs.detect("linux", ids: ["linuxmint", "ubuntu"])
    assert_equal :apt, b.id

    b = SystemPkgs.detect("linux", ids: ["rocky", "rhel", "centos"])
    assert_equal :dnf, b.id

    b = SystemPkgs.detect("linux", ids: ["endeavouros", "arch"])
    assert_equal :pacman, b.id
  end

  def test_detect_non_linux
    assert_equal :brew, SystemPkgs.detect("macos").id
    assert_equal :pkg, SystemPkgs.detect("freebsd").id
    assert_nil SystemPkgs.detect("plan9")
  end

  # An unknown distro still gets served if one of the tools is there:
  # the ID list will always be behind the world.
  def test_detect_unknown_distro_falls_back_to_probing
    called = []

    # Probe order is apt, dnf, pacman: make only "rpm" resolvable.
    probe = ->(c, **kw) {
      called << c
      c == "rpm" ? "/usr/bin/rpm" : nil
    }

    SystemPkgs.stub(:which, probe) do
      b = SystemPkgs.detect("linux", ids: ["weirdos"])
      assert_equal :dnf, b.id
    end

    # apt was tried first and rejected, rather than skipped.
    assert_includes called, "dpkg-query"
  end

  def test_detect_unknown_distro_without_tools_is_nil
    SystemPkgs.stub(:which, ->(c, **kw) { nil }) do
      assert_nil SystemPkgs.detect("linux", ids: ["weirdos"])
    end
  end
end


class TestSystemPkgsUtils < Minitest::Test

  include TestHelper

  def test_which_finds_in_extra_dirs
    Dir.mktmpdir do |d|
      exe = File.join(d, "mytool")
      File.write(exe, "#!/bin/sh\n")
      File.chmod(0755, exe)

      assert_nil SystemPkgs.which("mytool")
      assert_equal exe, SystemPkgs.which("mytool", extra_dirs: [d])
    end
  end

  def test_which_with_explicit_path
    assert_equal "/bin/sh", SystemPkgs.which("/bin/sh")
    assert_nil SystemPkgs.which("/nonexistent/thing")
  end

  # A directory named like the tool must not count as the tool.
  def test_which_ignores_directories
    Dir.mktmpdir do |d|
      FileUtils.mkdir_p(File.join(d, "mytool"))
      assert_nil SystemPkgs.which("mytool", extra_dirs: [d])
    end
  end

  def test_as_root_when_already_root
    Process.stub(:uid, 0) do
      assert_equal ["apt", "install", "x"],
                   SystemPkgs.as_root(["apt", "install", "x"])
    end
  end

  def test_as_root_prefers_sudo
    Process.stub(:uid, 1000) do
      sudo = ->(c, **kw) { c == "sudo" ? "/usr/bin/sudo" : nil }
      SystemPkgs.stub(:which, sudo) do
        assert_equal ["sudo", "apt", "install", "x"],
                     SystemPkgs.as_root(["apt", "install", "x"])
      end
    end
  end

  # su takes one string, so the argv has to be flattened and escaped.
  def test_as_root_falls_back_to_su_with_escaping
    Process.stub(:uid, 1000) do
      SystemPkgs.stub(:which, ->(c, **kw) { nil }) do
        got = SystemPkgs.as_root(["apt", "install", "a b"])
        assert_equal ["su", "-c", "apt install a\\ b"], got
      end
    end
  end

  def test_cmd_to_s_escapes
    assert_equal "apt install a\\ b",
                 SystemPkgs.cmd_to_s(["apt", "install", "a b"])
  end

  def test_run_capture_reads_stdout
    ok, out = SystemPkgs.run_capture(["echo", "hello"])
    assert_equal true, ok
    assert_equal "hello", out.strip
  end

  def test_run_capture_on_missing_command
    ok, out = SystemPkgs.run_capture(["/nonexistent/xyz"])
    assert_equal false, ok
    assert_equal "", out
  end

  def test_run_quiet
    assert_equal true, SystemPkgs.run_quiet(["true"])
    assert_equal false, SystemPkgs.run_quiet(["false"])
    assert_equal false, SystemPkgs.run_quiet(["/nonexistent/xyz"])
  end
end


class TestSysDepCommandChecks < Minitest::Test

  include TestHelper

  def test_command_present_and_new_enough
    env = FakeSysEnv.new(
      tools: { "rustc" => { path: "/home/u/.cargo/bin/rustc", ver: "1.97.1" } }
    )
    r = rustc_dep.check(env)
    assert_equal :ok, r.state
    assert r.ok?
    assert_equal "/home/u/.cargo/bin/rustc", r.path
  end

  # The case that motivates the whole design: Ubuntu 22.04's rustc.
  def test_command_present_but_too_old
    env = FakeSysEnv.new(
      tools: { "rustc" => { path: "/usr/bin/rustc", ver: "1.66.1" } }
    )
    r = rustc_dep.check(env)
    assert_equal :too_old, r.state
    refute r.ok?
    assert r.blocking?
    assert_includes r.detail, "1.66.1"
  end

  def test_command_missing
    r = rustc_dep.check(FakeSysEnv.new)
    assert_equal :missing, r.state
    assert_equal "not found", r.detail
  end

  # On disk but unrunnable, or runs and prints nothing we can parse.
  # Distinct from missing: installing the package does not fix it.
  def test_command_present_but_broken
    env = FakeSysEnv.new(
      tools: { "rustc" => { path: "/usr/bin/rustc", ver: nil } }
    )
    r = rustc_dep.check(env)
    assert_equal :broken, r.state
    assert r.blocking?
    assert_includes r.detail, "would not report a version"
  end

  def test_command_without_min_ver_only_needs_to_run
    dep = SystemDeps::SysDep.new(key: :dtc, what: "dtc", command: "dtc")
    env = FakeSysEnv.new(
      tools: { "dtc" => { path: "/usr/bin/dtc", ver: "1.6.0" } }
    )
    assert_equal :ok, dep.check(env).state
  end

  def test_exact_min_version_is_accepted
    env = FakeSysEnv.new(
      tools: { "rustc" => { path: "/x/rustc", ver: "1.85.0" } }
    )
    assert_equal :ok, rustc_dep("1.85").check(env).state
  end

  def test_label
    assert_equal "rustc >= 1.85", rustc_dep.label
    assert_equal "ssl", ssl_dep.label
  end

  # A version constraint we cannot check is worse than none: it would
  # read as satisfied on every distro.
  def test_min_ver_without_command_is_rejected
    err = assert_raises(RuntimeError) do
      SystemDeps::SysDep.new(key: :x, what: "x", min_ver: Ver("1.0"))
    end
    assert_includes err.message, "command"
  end
end


class TestSysDepPackageChecks < Minitest::Test

  include TestHelper

  def test_package_installed
    env = FakeSysEnv.new(
      backend: FakeSysBackend.new(id: :apt, installed: ["libssl-dev"])
    )
    r = ssl_dep.check(env)
    assert_equal :ok, r.state
    assert_equal ["libssl-dev"], env.backend.queried
  end

  def test_package_missing
    env = FakeSysEnv.new(backend: FakeSysBackend.new(id: :apt))
    assert_equal :missing, ssl_dep.check(env).state
  end

  # The right name per distro, not one name everywhere.
  def test_package_name_is_per_backend
    env = FakeSysEnv.new(
      backend: FakeSysBackend.new(id: :dnf, installed: ["openssl-devel"])
    )
    assert_equal :ok, ssl_dep.check(env).state
  end

  def test_package_name_as_plain_string_applies_everywhere
    dep = SystemDeps::SysDep.new(key: :bison, what: "bison", pkgs: "bison")
    assert_equal "bison", dep.pkg_for(:apt)
    assert_equal "bison", dep.pkg_for(:pacman)
  end

  # An unsupported distro is a thing we do not know, and the report
  # has to say so instead of guessing either way.
  def test_unknown_when_no_backend
    r = ssl_dep.check(FakeSysEnv.new(backend: nil))
    assert_equal :unknown, r.state
    refute r.blocking?
    assert_includes r.detail, "cannot check"
  end

  def test_unknown_when_backend_has_no_name_for_it
    env = FakeSysEnv.new(backend: FakeSysBackend.new(id: :pacman))
    assert_equal :unknown, ssl_dep.check(env).state
  end
end


class TestSystemDepsCollect < Minitest::Test

  include TestHelper

  def test_collect_dedups_and_records_who_asked
    rustc = rustc_dep
    pm = FakeSysPm.new(
      "a" => FakeSysPkg.new("a", [rustc]),
      "b" => FakeSysPkg.new("b", [rustc, ssl_dep]),
    )

    got = SystemDeps.collect([["a", nil], ["b", nil]], pm)

    assert_equal 2, got.length
    assert_equal :rustc, got[0][0].key
    assert_equal ["a", "b"], got[0][1]
    assert_equal :ssl, got[1][0].key
    assert_equal ["b"], got[1][1]
  end

  def test_collect_skips_unknown_packages
    pm = FakeSysPm.new({})
    assert_empty SystemDeps.collect([["ghost", nil]], pm)
  end

  def test_collect_empty_for_packages_without_deps
    pm = FakeSysPm.new("a" => FakeSysPkg.new("a", []))
    assert_empty SystemDeps.collect([["a", nil]], pm)
  end
end


class TestCheckPlan < Minitest::Test

  include TestHelper
  include SysDepCapture

  def pm_with(*deps)
    return FakeSysPm.new("p" => FakeSysPkg.new("p", deps))
  end

  def plan = [["p", nil]]

  def test_no_declared_deps_is_a_no_op
    env = FakeSysEnv.new
    assert_equal true,
                 SystemDeps.check_plan(plan, env: env, pm: pm_with())
    assert_empty env.ran
  end

  def test_all_satisfied
    env = FakeSysEnv.new(
      tools: { "rustc" => { path: "/x/rustc", ver: "1.97.1" } }
    )
    assert_equal true,
                 SystemDeps.check_plan(plan, env: env, pm: pm_with(rustc_dep))
    assert_empty env.ran
  end

  # -d is a question, not an action.
  def test_dry_run_reports_but_installs_nothing
    env = FakeSysEnv.new(backend: FakeSysBackend.new(id: :apt))
    ok = SystemDeps.check_plan(plan, dry_run: true, env: env,
                               pm: pm_with(ssl_dep))
    assert_equal true, ok
    assert_empty env.ran
    assert_empty env.asked
  end

  def test_installs_missing_package_after_confirmation
    backend = FakeSysBackend.new(id: :apt)
    env = FakeSysEnv.new(backend: backend, answers: [true])

    # Installing makes it present, so the recheck passes.
    env.on_run = ->(argv, e) { backend.instance_variable_set(
      :@installed, ["libssl-dev"]) }

    ok = SystemDeps.check_plan(plan, env: env, pm: pm_with(ssl_dep))

    assert_equal true, ok
    assert_equal [["fakepm", "install", "libssl-dev"]], env.ran
    assert_equal 1, env.asked.length
  end

  def test_declining_the_prompt_fails_the_run
    env = FakeSysEnv.new(backend: FakeSysBackend.new(id: :apt),
                         answers: [false])
    ok = SystemDeps.check_plan(plan, env: env, pm: pm_with(ssl_dep))
    assert_equal false, ok
    assert_empty env.ran
  end

  # Nobody is there to answer, and this is not CI: print the command
  # and stop, rather than installing packages unasked.
  def test_non_interactive_refuses_to_install
    env = FakeSysEnv.new(backend: FakeSysBackend.new(id: :apt),
                         interactive: false, ci: false)
    out = capture_out { SystemDeps.check_plan(plan, env: env,
                                              pm: pm_with(ssl_dep)) }

    assert_equal false, out[:result]
    assert_empty env.ran
    assert_empty env.asked

    # The exact command to run, so the refusal is actionable.
    assert_includes out[:text], "fakepm install libssl-dev"
  end

  def test_ci_installs_without_asking_and_passes_assume_yes
    backend = FakeSysBackend.new(id: :apt)
    env = FakeSysEnv.new(backend: backend, interactive: false, ci: true)
    env.on_run = ->(argv, e) { backend.instance_variable_set(
      :@installed, ["libssl-dev"]) }

    ok = SystemDeps.check_plan(plan, env: env, pm: pm_with(ssl_dep))

    assert_equal true, ok
    assert_equal [["fakepm", "install", "-y", "libssl-dev"]], env.ran
    assert_empty env.asked
  end

  # The install command exiting 0 is not evidence the requirement is
  # met: a package manager will happily install a rustc that is still
  # too old.
  def test_recheck_catches_an_install_that_did_not_help
    backend = FakeSysBackend.new(id: :apt)
    dep = SystemDeps::SysDep.new(
      key: :rustc, what: "Rust", command: "rustc", min_ver: Ver("1.85"),
      pkgs: { apt: "rustc" }
    )
    env = FakeSysEnv.new(backend: backend, answers: [true])

    # apt "installs" rustc -- but the one it installs is 1.66.
    env.on_run = ->(argv, e) {
      e.tools["rustc"] = { path: "/usr/bin/rustc", ver: "1.66.1" }
    }

    ok = SystemDeps.check_plan(plan, env: env, pm: pm_with(dep))
    assert_equal false, ok
  end

  # Nothing can install it and no installer covers it: say so plainly
  # instead of pretending the build might work.
  def test_unfixable_dep_fails
    dep = SystemDeps::SysDep.new(key: :magic, what: "a magic tool",
                                 command: "magic")
    env = FakeSysEnv.new(backend: FakeSysBackend.new(id: :apt))
    ok = SystemDeps.check_plan(plan, env: env, pm: pm_with(dep))
    assert_equal false, ok
    assert_empty env.ran
  end

  # :unknown means we could not check, which is not grounds for
  # refusing to build.
  def test_unknown_only_warns_and_continues
    env = FakeSysEnv.new(backend: nil)
    ok = SystemDeps.check_plan(plan, env: env, pm: pm_with(ssl_dep))
    assert_equal true, ok
    assert_empty env.ran
  end
end


class TestRemediationRouting < Minitest::Test

  include TestHelper

  # A dep whose distro package is TOO OLD must not be routed to the
  # package manager: reinstalling what is already there fixes nothing
  # and hides the real answer, which is "use rustup".
  def test_too_old_is_not_routed_to_the_package_manager
    dep = SystemDeps::SysDep.new(
      key: :rustc, what: "Rust", command: "rustc", min_ver: Ver("1.85"),
      pkgs: { apt: "rustc" }, installer: SystemDeps::RUSTUP
    )
    env = FakeSysEnv.new(
      backend: FakeSysBackend.new(id: :apt),
      tools: { "rustc" => { path: "/usr/bin/rustc", ver: "1.66.1" } }
    )
    r = dep.check(env)
    by_pm, rest = SystemDeps.split_by_remedy([[r, ["p"]]], env.backend)

    assert_empty by_pm
    assert_equal 1, rest.length
  end

  def test_missing_with_a_package_name_goes_to_the_package_manager
    dep = SystemDeps::SysDep.new(
      key: :rustc, what: "Rust", command: "rustc",
      pkgs: { apt: "rustc" }
    )
    env = FakeSysEnv.new(backend: FakeSysBackend.new(id: :apt))
    r = dep.check(env)
    by_pm, rest = SystemDeps.split_by_remedy([[r, ["p"]]], env.backend)

    assert_equal 1, by_pm.length
    assert_empty rest
  end

  def test_without_a_backend_everything_goes_to_the_installers
    env = FakeSysEnv.new(backend: nil)
    r = rustc_dep.check(env)
    by_pm, rest = SystemDeps.split_by_remedy([[r, ["p"]]], nil)

    assert_empty by_pm
    assert_equal 1, rest.length
  end
end


class TestRustupInstaller < Minitest::Test

  include TestHelper
  include SysDepCapture

  def rust_plan = [["p", nil]]
  def rust_pm = FakeSysPm.new("p" => FakeSysPkg.new("p", [rustc_dep]))

  def installed_rust
    return ->(argv, e) {
      e.tools["rustc"] = { path: "/h/.cargo/bin/rustc", ver: "1.97.1" }
    }
  end

  # Same policy the host package manager gets: CI proceeds. Splitting
  # the two -- letting CI install packages but not a toolchain -- only
  # produces CI that cannot build what it was asked to build.
  def test_ci_installs_rustup_without_asking
    env = FakeSysEnv.new(interactive: false, ci: true)
    env.on_run = installed_rust

    ok = SystemDeps.check_plan(rust_plan, env: env, pm: rust_pm)

    assert_equal true, ok
    assert_equal 2, env.ran.length
    assert_equal "curl", env.ran[0][0]
    assert_includes env.ran[0], "https://sh.rustup.rs"
    assert_equal "sh", env.ran[1][0]
    assert_empty env.asked
  end

  # No terminal and no declaration of being unattended: refuse rather
  # than act on somebody's machine with nobody watching.
  def test_no_tty_and_no_ci_refuses
    env = FakeSysEnv.new(interactive: false, ci: false)
    out = capture_out { SystemDeps.check_plan(rust_plan, env: env,
                                              pm: rust_pm) }

    assert_equal false, out[:result]
    assert_empty env.ran
    assert_empty env.asked
    assert_includes out[:text], "Install rustc >= 1.85 yourself"
  end

  # Installing what the build needs is the expected answer, exactly as
  # it is for apt.
  def test_prompt_defaults_to_yes
    env = FakeSysEnv.new(interactive: true)
    env.on_run = installed_rust
    seen = []
    env.define_singleton_method(:ask) do |q, default: true|
      seen << [q, default]
      default
    end

    ok = SystemDeps.check_plan(rust_plan, env: env, pm: rust_pm)

    assert_equal true, ok
    assert_equal true, seen[0][1]
    assert_includes seen[0][0], "Install"
  end

  def test_declining_rustup_fails_the_run
    env = FakeSysEnv.new(interactive: true, answers: [false])
    ok = SystemDeps.check_plan(rust_plan, env: env, pm: rust_pm)
    assert_equal false, ok
    assert_empty env.ran
  end

  def test_download_failure_is_reported
    env = FakeSysEnv.new(interactive: true, answers: [true, false],
                         run_result: false)
    ok = SystemDeps.check_plan(rust_plan, env: env, pm: rust_pm)
    assert_equal false, ok
    assert_equal 1, env.ran.length   # stopped after the failed download
  end

  #
  # The PATH question. Somebody installing Rust probably wants it
  # outside this build too, so it is asked rather than decided.
  #

  def test_accepting_the_path_question_lets_rustup_update_the_profile
    env = FakeSysEnv.new(interactive: true, answers: [true, true])
    env.on_run = installed_rust

    SystemDeps.check_plan(rust_plan, env: env, pm: rust_pm)

    sh = env.ran.find { |a| a[0] == "sh" }
    assert_includes sh, "-y"
    refute_includes sh, "--no-modify-path"
    assert_equal 2, env.asked.length
    assert_includes env.asked[1], "PATH"
  end

  def test_declining_the_path_question_leaves_the_profile_alone
    env = FakeSysEnv.new(interactive: true, answers: [true, false])
    env.on_run = installed_rust

    out = capture_out { SystemDeps.check_plan(rust_plan, env: env,
                                              pm: rust_pm) }

    sh = env.ran.find { |a| a[0] == "sh" }
    assert_includes sh, "--no-modify-path"

    # ...and says how to do it by hand instead.
    assert_includes out[:text], "export PATH="
  end

  # Nobody to ask means nobody consented: an unattended run must not
  # edit a shell profile.
  def test_unattended_never_touches_the_profile
    env = FakeSysEnv.new(interactive: false, ci: true)
    env.on_run = installed_rust

    SystemDeps.check_plan(rust_plan, env: env, pm: rust_pm)

    sh = env.ran.find { |a| a[0] == "sh" }
    assert_includes sh, "--no-modify-path"
  end

  def test_installer_metadata
    assert_equal :rustup, SystemDeps::RUSTUP.id
    assert_includes SystemDeps::RUSTUP.bin_dir, ".cargo"
    refute_respond_to SystemDeps::RUSTUP, :env_opt_in
  end

  # The floor is set by what we build, and the coupling is easy to
  # lose: gdk-pixbuf 2.44.8 wants glycin-2 >= 2.2.alpha.7, whose
  # Cargo.toml declares rust-version = "1.93".
  def test_min_rust_matches_what_glycin_requires
    assert_equal Ver("1.93"), SystemDeps::MIN_RUST
  end
end


class TestSystemDepsMisc < Minitest::Test

  include TestHelper

  # rustup's toolchain is invisible to a non-login shell when it was
  # installed with --no-modify-path, so PATH alone is not enough.
  def test_extra_bin_dirs_only_returns_existing_dirs
    dirs = SystemDeps.extra_bin_dirs
    assert dirs.all? { |d| File.directory?(d) }
  end

  def test_package_default_has_no_system_deps
    with_fake_tc do
      reset_pkgmgr!
      pkg = TestHelper::FakePackage.new("whatever")
      assert_empty pkg.system_deps
      assert_empty pkg.system_deps(Ver("1.0"))
    end
  end

  # One switch drives the meson option AND the Rust requirement, so
  # the flag cannot be flipped without the check following it. Tested
  # in both directions rather than against whatever it is set to
  # today: the coupling is the invariant, the value is a decision.
  def test_gdk_pixbuf_rust_requirement_follows_the_glycin_switch
    pkg = HostGdkPixbufPackage.new

    HostGdkPixbufPackage.stub_const_with_glycin(true) do
      deps = pkg.system_deps
      assert_equal [:rustc, :cargo], deps.map(&:key)
      assert deps.all? { |d| d.installer == SystemDeps::RUSTUP }
    end

    HostGdkPixbufPackage.stub_const_with_glycin(false) do
      assert_empty pkg.system_deps
    end
  end

  # And the decision itself, stated once so that turning glycin off
  # is a deliberate edit to a test rather than a silent drift.
  def test_glycin_is_currently_enabled_so_rust_is_required
    assert_equal true, HostGdkPixbufPackage::WITH_GLYCIN
    assert_equal [:rustc, :cargo],
                 HostGdkPixbufPackage.new.system_deps.map(&:key)
  end
end

# Flip WITH_GLYCIN for one block. A constant rather than a method
# because it is read from a string-interpolated meson argument too,
# and the point of the test is that ONE switch drives both.
class HostGdkPixbufPackage
  def self.stub_const_with_glycin(val)
    old = const_get(:WITH_GLYCIN)
    old_verbose = $VERBOSE
    $VERBOSE = nil
    const_set(:WITH_GLYCIN, val)
    $VERBOSE = old_verbose
    yield
  ensure
    $VERBOSE = nil
    const_set(:WITH_GLYCIN, old)
    $VERBOSE = old_verbose
  end
end


class TestAskYesNo < Minitest::Test

  include TestHelper

  def ask(input, default: true)
    with_context(STDIN: StringIO.new(input)) do
      Term.ask_yes_no("Proceed?", default: default)
    end
  end

  def test_explicit_answers
    assert_equal true, ask("y\n")
    assert_equal true, ask("YES\n")
    assert_equal false, ask("n\n", default: true)
    assert_equal false, ask("nope\n")
  end

  def test_empty_answer_takes_the_default
    assert_equal true, ask("\n", default: true)
    assert_equal false, ask("\n", default: false)
  end

  # EOF -- a pipe, a closed stdin -- must not block forever.
  def test_eof_takes_the_default
    assert_equal true, ask("", default: true)
    assert_equal false, ask("", default: false)
  end
end


#
# The glue that talks to the real machine. Faked everywhere above, so
# these are the tests that would notice if it stopped working.
#
class TestRealEnvironmentGlue < Minitest::Test

  include TestHelper

  def test_distro_ids_from_os_release
    fake = { "ID" => "Ubuntu", "ID_LIKE" => "debian" }
    InitOnly.stub(:parse_os_release, fake) do
      assert_equal ["ubuntu", "debian"], SystemPkgs.distro_ids
    end
  end

  def test_distro_ids_without_id_like
    InitOnly.stub(:parse_os_release, { "ID" => "fedora" }) do
      assert_equal ["fedora"], SystemPkgs.distro_ids
    end
  end

  def test_distro_ids_splits_multi_word_id_like
    fake = { "ID" => "rocky", "ID_LIKE" => "rhel centos fedora" }
    InitOnly.stub(:parse_os_release, fake) do
      assert_equal ["rocky", "rhel", "centos", "fedora"],
                   SystemPkgs.distro_ids
    end
  end

  # Detection runs a subprocess per probe; doing it once per run is
  # the difference between one dpkg-query and one per dependency.
  def test_backend_is_memoized
    SystemPkgs.instance_variable_set(:@backend, nil)
    calls = 0

    SystemPkgs.stub(:detect, -> { calls += 1; :fake_backend }) do
      assert_equal :fake_backend, SystemPkgs.backend
      assert_equal :fake_backend, SystemPkgs.backend
    end

    assert_equal 1, calls
  ensure
    SystemPkgs.instance_variable_set(:@backend, nil)
  end

  def test_in_ci_reads_both_conventions
    with_env("RUNNING_IN_CI" => nil, "CI" => nil) do
      assert_equal false, SystemPkgs.in_ci?
    end
    with_env("RUNNING_IN_CI" => "1", "CI" => nil) do
      assert_equal true, SystemPkgs.in_ci?
    end
    with_env("RUNNING_IN_CI" => nil, "CI" => "true") do
      assert_equal true, SystemPkgs.in_ci?
    end
    # Set-but-empty is not "in CI".
    with_env("RUNNING_IN_CI" => "", "CI" => "  ") do
      assert_equal false, SystemPkgs.in_ci?
    end
  end

  def test_run_quiet_survives_a_bad_argv
    assert_equal false, SystemPkgs.run_quiet([""])
  end

  # The real Env, against real commands: this is what the faked
  # `which` and `probe_version` above stand in for.
  def test_real_env_which_and_probe_version
    env = SystemDeps::Env.new
    sh = env.which("sh")

    refute_nil sh
    assert File.executable?(sh)

    ver = env.probe_version(RbConfig.ruby, "--version",
                            SystemDeps::SysDep::DEFAULT_VER_RE)
    refute_nil ver
    assert ver >= Ver("3.2.0"), "got #{ver}"
  end

  def test_real_env_probe_version_on_a_command_that_fails
    env = SystemDeps::Env.new
    got = env.probe_version("/nonexistent/xyz", "--version",
                            SystemDeps::SysDep::DEFAULT_VER_RE)
    assert_nil got
  end

  # Runs, but prints nothing with a version in it.
  def test_real_env_probe_version_on_unparseable_output
    env = SystemDeps::Env.new
    got = env.probe_version("/bin/echo", "--version",
                            /\Ano-such-version\z/)
    assert_nil got
  end

  # The abstract Backend refuses to guess.
  def test_backend_base_class_is_abstract
    b = SystemPkgs::Backend.new(:fake, "nope")
    assert_raises(NotImplementedError) { b.query_argv("x") }
    assert_raises(NotImplementedError) { b.install_argv(["x"]) }
    assert_equal false, b.available?
  end

  def test_installer_base_class_is_abstract
    inst = SystemDeps::Installer.new(id: :x, what: "x", url: "http://x")
    assert_raises(NotImplementedError) { inst.run(nil) }
  end

  # The rustup script downloaded fine but exited non-zero.
  def test_rustup_install_failure_is_reported
    env = FakeSysEnv.new(interactive: true, answers: [true])
    env.on_run = ->(argv, e) { e.run_result = (argv[0] == "curl") }

    pm = FakeSysPm.new("p" => FakeSysPkg.new("p", [rustc_dep]))
    ok = SystemDeps.check_plan([["p", nil]], env: env, pm: pm)

    assert_equal false, ok
    assert_equal 2, env.ran.length
    assert_equal "sh", env.ran[1][0]
  end

  def with_env(vars)
    saved = vars.keys.to_h { |k| [k, ENV[k]] }
    vars.each { |k, v| ENV[k] = v }
    yield
  ensure
    saved.each { |k, v| ENV[k] = v }
  end
end
