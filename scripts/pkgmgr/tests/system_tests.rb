# SPDX-License-Identifier: BSD-2-Clause
#
# System tests: install all packages and build Tilck, once per target
# -- the archs -a names and the boards -b names -- optionally through
# every build-generator configuration and Tilck's own tests.
#
# When $dry_run is set, the exact same code path executes but every
# action prints [ DRY ] instead of running. Timing works in both modes.
#

require 'pathname'
require 'fileutils'
require_relative '../term'
require_relative '../early_logic'   # DEFAULT_TC_NAME
require_relative '../package_manager'
require_relative 'test_helper'      # real_world!

module SystemTests

  include Term

  # The harness as an object, so its helpers can be called from a
  # module rather than from inside a Minitest::Test. Same handle the
  # exhaustive lane uses (tests/exhaustive/runner.rb).
  class Harness
    include TestHelper
  end

  module_function

  DRY_TAG = "#{CYAN256}[ DRY ]#{RESET}"

  # --- Paths ---

  MAIN_DIR   = Pathname.new(File.expand_path("../../..", __dir__))
  BTC        = (MAIN_DIR / "scripts" / "build_toolchain").to_s
  CMAKE_RUN  = (MAIN_DIR / "scripts" / "cmake_run").to_s
  BUILDS_DIR = MAIN_DIR / "other_builds"
  GEN_DIR    = MAIN_DIR / "scripts" / "build_generators"

  # --- Architecture constants ---

  ALL_TEST_ARCHS  = ["i386", "x86_64", "riscv64"]
  TILCK_TEST_ARCHS = ["i386", "riscv64"]
  DEFAULT_ARCH = ENV["ARCH"].to_s.empty? ? "i386" : ENV["ARCH"]

  # --- Package constants ---

  # Per-target package sets come from `build_toolchain --list-installable`
  # at runtime (see installable_pkg_tags below). Keeping a hardcoded
  # OPTIONAL_PACKAGES here would drift as packages are added/removed.

  # The CMake flag that builds each optional package into the image,
  # by the package's name: a flag is passed when its package was
  # installed for the target being built. By name and not by path --
  # the paths this table held were toolchain4's, and every flag had
  # quietly stopped being passed when the layout changed.
  EXTRA_FLAG_MAP = {
    "EXTRA_VIM"          => "vim",
    "EXTRA_TCC"          => "tcc",
    "EXTRA_FBDOOM"       => "fbdoom",
    "EXTRA_MICROPYTHON"  => "micropython",
    "EXTRA_LUA"          => "lua",
    "EXTRA_TREE_CMD"     => "treecmd",
    "EXTRA_TFBLIB"       => "tfblib",
  }

  # --- Targets ---

  def resolve_archs(arch)
    return ALL_TEST_ARCHS if arch == "ALL"
    return [arch] if arch
    [DEFAULT_ARCH]
  end

  # The [arch, board] pairs a run is about: -a's archs, at the board
  # -b names, every board of each for -b ALL, the arch's default
  # otherwise. A board belongs to one arch: a name beside -a ALL, or
  # one the arch does not have, is refused before anything is wiped.
  def resolve_targets(arch, board)
    if board && board != "ALL" && arch == "ALL"
      fail!("-b #{board} names one arch's board: with -a ALL, use -b ALL")
    end
    return resolve_archs(arch).flat_map { |name|
      a = ALL_ARCHS.fetch(name)
      boards = if board == "ALL" then a.all_boards
               elsif board then [board]
               else [a.default_board]
               end
      if (bad = boards.find { |b| !a.all_boards.include?(b) })
        fail!("Unknown board #{bad} for #{name}")
      end
      boards.map { |b| [name, b] }
    }
  end

  # --- Coverage plumbing ---

  # Directory for subprocess coverage JSON files. Set by run_all.rb.
  COVERAGE_DIR = ENV["COVERAGE_DIR"]

  # Base env hash for subprocesses. Includes COVERAGE_DIR when the
  # test runner has coverage enabled, so subprocess installs also
  # collect coverage data.
  def base_env(arch_name, board)
    env = { "ARCH" => arch_name, "BOARD" => board }
    env["COVERAGE_DIR"] = COVERAGE_DIR if COVERAGE_DIR
    env
  end

  # --- Timing ---

  def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  def fmt_elapsed(t) = "%.1fs" % t

  def section(name)
    puts
    puts HLINE
    puts "  #{BOLD}#{name}#{RESET}"
    puts HLINE
    t0 = now
    yield
    puts
    puts "  #{DIM}#{name}: #{fmt_elapsed(now - t0)}#{RESET}"
  end

  # --- Output ---

  def step(msg)
    print "  #{msg}... "
    $stdout.flush
  end

  def ok(elapsed = nil)
    t = elapsed ? "  #{DIM}(#{fmt_elapsed(elapsed)})#{RESET}" : ""
    puts "#{GREEN256}OK#{RESET}#{t}"
  end

  def dry   = puts(DRY_TAG)

  def fail!(msg)
    puts "#{RED256}FAILED#{RESET}"
    $stderr.puts "#{RED256}ERROR: #{msg}#{RESET}"
    exit 1
  end

  # --- Primitive actions ---

  def run_cmd(desc, cmd, log: nil, env: {})
    step(desc)

    if $dry_run
      return dry
    end

    t0 = now
    ok2 = log ? system(env, *cmd, out: log, err: log)
              : system(env, *cmd, out: "/dev/null", err: "/dev/null")

    fail!("#{desc} failed" + (log ? " (see #{log})" : "")) if !ok2
    ok(now - t0)
  end

  def prepare_build_dir(dir)
    return if $dry_run
    FileUtils.rm_rf(dir)
    FileUtils.mkdir_p(dir)
  end

  def in_build_dir(dir)
    prepare_build_dir(dir)
    Dir.chdir($dry_run ? "." : dir) { yield }
  end

  # --- Compound actions ---

  # Remove everything this test is about to reinstall, and nothing
  # else -- once, before the first target: what one target installs
  # is not what another builds from, so a wipe per target proved
  # nothing more and left only the last target's packages behind.
  #
  # Through the package manager, not by walking directories. The hand
  # rolled version kept a child called "host" and deleted the rest --
  # which was the toolchain4 layout. Under toolchain5 the children are
  # linux-x86_64, noarch, tilck-*, and there is no "host", so the
  # branch that preserved the bootstrap Ruby never ran and the wipe
  # deleted the interpreter it was running on. A second copy of the
  # layout in a place that is not Coords, exactly like the one CMake
  # had before it started asking --print-layout.
  #
  # The HOST WORLD is kept: our own GCC, the QEMU built with it, and
  # the fifty-odd packages nothing else needs. This test exists to
  # prove that the packages Tilck is built FROM build on this machine,
  # which is the package manager's daily job. Rebuilding a compiler
  # and a GTK stack to answer that question costs hours and answers
  # nothing about it.
  def wipe_toolchain
    step("Wipe toolchain (keep cache, Ruby and the host world)")

    if $dry_run
      return dry
    end

    t0 = now

    # force: the cross compilers come back too -- installing them is
    # part of what this test checks. Ruby is protected by name inside
    # the package manager, which is the only handle it has.
    pkgmgr.refresh
    pkgmgr.clean(false, except: pkgmgr.host_world_names, force: true)
    ok(now - t0)
  end

  # Install every package the target can take, in dependency order,
  # one -s per package. Returns the names installed, for the flags
  # the build is given.
  def install_packages(arch_name, board, packages_filter: nil)
    env = base_env(arch_name, board)
    installable = installable_pkg_tags(arch_name, board)

    if packages_filter
      re = Regexp.new(packages_filter)
      installable = installable.select { |name, _| name.match?(re) }
    end

    # The host world is skipped, not installed and not removed: see
    # wipe_toolchain. --list-installable tags it, so this is one
    # comparison rather than a list to keep in step.
    installable = installable.reject { |_, tag| tag == "host-world" }

    installable.each { |name, tag|
      suffix = (tag == "default") ? " (default)" : ""
      run_cmd("Install #{name}#{suffix}",
              [BTC, "-q", "-n", "-s", name], env: env)
    }
    return installable.map(&:first)
  end

  # Ask the pkgmgr (in a fresh subprocess) for the ordered list of
  # packages installable on `arch_name` at `board`, with each entry tagged as
  # "default" (auto-installed by `build_toolchain` with no args,
  # either as an explicit default or a transitive dep of one) or
  # "optional" (opt-in only). Returns an array of [name, tag] pairs
  # in topological install order (deps before dependents), so a
  # consumer iterating `-s` per package keeps each step narrow.
  def installable_pkg_tags(arch_name, board)
    env = base_env(arch_name, board).merge("QUIET" => "1")
    out = IO.popen(env, [BTC, "-q", "--list-installable"],
                   err: "/dev/null", &:read)
    out.split("\n").reject(&:empty?).map { |line|
      name, tag = line.split(" ", 2)
      [name, tag]
    }
  end

  def extra_cmake_flags(installed)
    EXTRA_FLAG_MAP.filter_map { |flag, name|
      "-D#{flag}=1" if installed.include?(name)
    }
  end

  def cmake_and_build(arch_name, board, build_dir, installed)
    env    = { "ARCH" => arch_name, "BOARD" => board }
    extras = extra_cmake_flags(installed)

    # The full list of -DEXTRA_*=1 flags is kept out of the step
    # label (it can reach 7+ flags, too long to scan at a glance)
    # and preserved in cmake.log for debugging.
    run_cmd("cmake", [CMAKE_RUN] + extras,
            log: "#{build_dir}/cmake.log", env: env)

    run_cmd("make -j", ["make", "-j"],
            log: "#{build_dir}/build.log", env: env)

    run_cmd("make -j gtests", ["make", "-j", "gtests"],
            log: "#{build_dir}/gtests_build.log", env: env)
  end

  def run_tilck_tests(build_dir)
    gtests_bin  = "#{build_dir}/gtests"
    test_runner = "#{build_dir}/st/run_all_tests"

    if $dry_run || File.exist?(gtests_bin)
      run_cmd("run gtests", [gtests_bin],
              log: "#{build_dir}/gtests_run.log")
    end

    if $dry_run || File.exist?(test_runner)
      run_cmd("system tests -c", [test_runner, "-c"],
              log: "#{build_dir}/systests.log")
    else
      $stderr.puts "  #{DIM}WARNING: test runner not found#{RESET}"
    end
  end

  # Tilck's own tests boot the image under QEMU, which is what the
  # arch's default board is: an image for other hardware has nothing
  # to boot on here.
  def maybe_run_tilck_tests(arch_name, board, build_dir, run_tilck)
    if run_tilck && TILCK_TEST_ARCHS.include?(arch_name) &&
       board == ALL_ARCHS.fetch(arch_name).default_board
      run_tilck_tests(build_dir)
    end
  end

  # --- Per-target phases ---

  def do_install(arch_name, board, packages_filter)
    installed = nil
    section("Install packages") do
      installed = install_packages(arch_name, board,
                                   packages_filter: packages_filter)
    end
    return installed
  end

  def do_default_build(arch_name, board, run_tilck, installed)
    build_dir = (BUILDS_DIR / "systest_#{arch_name}_#{board}").to_s

    section("Default build") do
      in_build_dir(build_dir) do
        cmake_and_build(arch_name, board, build_dir, installed)
        maybe_run_tilck_tests(arch_name, board, build_dir, run_tilck)
      end
    end
  end

  def do_generator_build(arch_name, board, gen_name, run_tilck)
    gen_script = (GEN_DIR / gen_name).to_s
    build_dir  = (BUILDS_DIR / "#{gen_name}_#{arch_name}_#{board}").to_s
    env        = { "ARCH" => arch_name, "BOARD" => board }

    section(gen_name) do
      in_build_dir(build_dir) do
        step("generator #{gen_name}")

        if $dry_run
          dry
        else
          t0 = now
          success = system(env, gen_script,
                           out: "#{build_dir}/cmake.log",
                           err: "#{build_dir}/cmake.log")

          if !success || File.exist?("skipped")
            puts "#{DIM}skipped#{RESET}"
            next
          end
          ok(now - t0)
        end

        run_cmd("make -j", ["make", "-j"],
                log: "#{build_dir}/build.log", env: env)

        maybe_run_tilck_tests(arch_name, board, build_dir, run_tilck)
      end
    end
  end

  def do_all_generators(arch_name, board, run_tilck)
    Dir.children(GEN_DIR).sort.each { |gen_name|
      do_generator_build(arch_name, board, gen_name, run_tilck)
    }
  end

  # --- Main entry point ---

  def run(run_tilck: false, all_build_types: false,
          arch: nil, board: nil, packages_filter: nil)

    # Everything below drives `build_toolchain` as a subprocess, which
    # gets a world of its own -- except wipe_toolchain, which asks this
    # process. The unit lane ran first and left it holding its own
    # fakes; this takes the real one back. See TestHelper#real_world!.
    Harness.new.real_world!

    grand_t0 = now
    FileUtils.mkdir_p(BUILDS_DIR) if !$dry_run
    targets = resolve_targets(arch, board)

    section("Wipe") { wipe_toolchain }

    targets.each { |arch_name, b|

      section("Target: #{arch_name}/#{b}") do
        installed = do_install(arch_name, b, packages_filter)
        do_default_build(arch_name, b, run_tilck, installed)
        do_all_generators(arch_name, b, run_tilck) if all_build_types
      end
    }


    puts
    puts HLINE
    puts "  #{BOLD}Total system test time: " \
         "#{fmt_elapsed(now - grand_t0)}#{RESET}"
    puts HLINE
  end
end
