# SPDX-License-Identifier: BSD-2-Clause
#
# System tests: install all packages, build Tilck for all architectures,
# optionally run all build-generator configurations and Tilck's own tests.
#
# This module is called from run_all.rb AFTER unit tests pass.
# All package operations go through build_toolchain as a subprocess
# (clean PackageManager, no test pollution).
#
# When $dry_run is set, the exact same code path executes but every
# action prints [ DRY ] instead of running.
#

require 'pathname'
require 'fileutils'

module SystemTests

  module_function

  # xterm-256 colors (match PrettyReporter)
  ESC   = "\e["
  GREEN = "#{ESC}38;5;40m"
  RED   = "#{ESC}38;5;196m"
  CYAN  = "#{ESC}38;5;75m"
  BOLD  = "#{ESC}1m"
  DIM   = "#{ESC}2m"
  RESET = "#{ESC}0m"
  HLINE = "#{DIM}#{"─" * 72}#{RESET}"

  DRY_TAG = "#{CYAN}[ DRY ]#{RESET}"

  # Project root (derive from this file's location)
  MAIN_DIR = Pathname.new(File.expand_path("../../..", __dir__))
  TC = MAIN_DIR / "toolchain4"
  BTC = (MAIN_DIR / "scripts" / "build_toolchain").to_s
  CMAKE_RUN = (MAIN_DIR / "scripts" / "cmake_run").to_s

  # Architectures to test. aarch64 excluded (no kernel support yet).
  ALL_TEST_ARCHS = ["i386", "x86_64", "riscv64"]

  # Default arch when none is specified (matches ARCH env or i386).
  DEFAULT_ARCH = ENV["ARCH"].to_s.empty? ? "i386" : ENV["ARCH"]

  # Architectures where Tilck tests can run (kernel is complete).
  TILCK_TEST_ARCHS = ["i386", "riscv64"]

  # All optional (non-default) packages that can be installed per arch.
  OPTIONAL_PACKAGES = %w[
    ncurses vim tcc fbdoom micropython lua treecmd tfblib
    gtest_src host_gtest lcov libmusl
  ]

  # Map of EXTRA_* cmake flags → the package dir to check.
  EXTRA_FLAG_MAP = {
    "EXTRA_VIM"          => ->(arch) { TC / "gcc-13.3.0" / arch / "vim" },
    "EXTRA_TCC"          => ->(arch) { TC / "gcc-13.3.0" / arch / "tcc" },
    "EXTRA_FBDOOM"       => ->(arch) { TC / "gcc-13.3.0" / arch / "fbdoom" },
    "EXTRA_MICROPYTHON"  => ->(arch) { TC / "gcc-13.3.0" / arch / "micropython" },
    "EXTRA_LUA"          => ->(arch) { TC / "gcc-13.3.0" / arch / "lua" },
    "EXTRA_TREE_CMD"     => ->(arch) { TC / "gcc-13.3.0" / arch / "treecmd" },
    "EXTRA_TFBLIB"       => ->(arch) { TC / "noarch" / "tfblib" },
  }

  # Resolve --test-arch into a list of architectures.
  def resolve_archs(arch)
    return ALL_TEST_ARCHS if arch == "ALL"
    return [arch] if arch
    [DEFAULT_ARCH]
  end

  def banner(msg)
    puts
    puts HLINE
    puts "  #{BOLD}#{msg}#{RESET}"
    puts HLINE
  end

  def step(msg)
    print "  #{msg}... "
    $stdout.flush
  end

  def ok(elapsed = nil)
    t = elapsed ? "  #{DIM}(#{elapsed})#{RESET}" : ""
    puts "#{GREEN}OK#{RESET}#{t}"
  end

  def dry
    puts DRY_TAG
  end

  def fail!(msg)
    puts "#{RED}FAILED#{RESET}"
    $stderr.puts "#{RED}ERROR: #{msg}#{RESET}"
    exit 1
  end

  def timed
    t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    yield
    "%.1fs" % (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0)
  end

  def run_cmd(desc, cmd, log: nil, env: {})
    step(desc)

    if $dry_run
      dry
      return
    end

    elapsed = timed {
      if log
        ok2 = system(env, *cmd, out: log, err: log)
      else
        ok2 = system(env, *cmd, out: "/dev/null", err: "/dev/null")
      end

      if !ok2
        fail!("#{desc} failed" + (log ? " (see #{log})" : ""))
      end
    }
    ok(elapsed)
  end

  def wipe_toolchain
    step("Wipe toolchain (keep cache + Ruby)")

    if $dry_run
      dry
      return
    end

    elapsed = timed {
      Dir.children(TC).each { |child|
        next if child == "cache" || child == "host"
        FileUtils.rm_rf(TC / child)
      }
    }
    ok(elapsed)
  end

  def install_packages(arch_name, packages_filter: nil)
    env = { "ARCH" => arch_name, "BOARD" => nil }

    step("Install default packages for #{arch_name}")
    if $dry_run
      dry
    else
      elapsed = timed {
        ok2 = system(env, BTC, "-q", "-n",
                     out: "/dev/null", err: "/dev/null")
        fail!("Default install failed for #{arch_name}") if !ok2
      }
      ok(elapsed)
    end

    # Optional packages
    pkgs = OPTIONAL_PACKAGES
    if packages_filter
      re = Regexp.new(packages_filter)
      pkgs = pkgs.select { |p| p.match?(re) }
    end

    for pkg in pkgs
      step("Install optional: #{pkg}")

      if $dry_run
        dry
        next
      end

      success = false
      elapsed = timed {
        success = system(env, BTC, "-q", "-n", "-s", pkg,
                         out: "/dev/null", err: "/dev/null")
      }
      if success
        ok(elapsed)
      else
        puts "#{DIM}skipped#{RESET}"
      end
    end
  end

  # Compute EXTRA_* cmake flags based on what's actually installed.
  # In dry-run mode, show all possible flags (can't check filesystem).
  def extra_cmake_flags(arch_name)
    flags = []
    EXTRA_FLAG_MAP.each do |flag, path_proc|
      if $dry_run
        flags << "-D#{flag}=1"
      else
        dir = path_proc.call(arch_name)
        if dir.directory? && !Dir.empty?(dir)
          flags << "-D#{flag}=1"
        end
      end
    end
    flags
  end

  def cmake_and_build(arch_name, build_dir)
    cmake_log = File.join(build_dir, "cmake.log")
    build_log = File.join(build_dir, "build.log")
    gtests_log = File.join(build_dir, "gtests_build.log")
    env = { "ARCH" => arch_name }

    extras = extra_cmake_flags(arch_name)
    extras_desc = extras.empty? ? "" : " " + extras.join(" ")

    run_cmd(
      "cmake#{extras_desc}",
      [CMAKE_RUN] + extras,
      log: cmake_log,
      env: env
    )

    run_cmd("make -j", ["make", "-j"], log: build_log, env: env)

    run_cmd("make -j gtests", ["make", "-j", "gtests"],
            log: gtests_log, env: env)
  end

  def run_tilck_tests(arch_name, build_dir)
    gtests_log = File.join(build_dir, "gtests_run.log")
    systests_log = File.join(build_dir, "systests.log")

    # In dry-run, the binary won't exist — always show the step.
    gtests_bin = File.join(build_dir, "gtests")
    if $dry_run || File.exist?(gtests_bin)
      run_cmd("run gtests", [gtests_bin], log: gtests_log)
    end

    test_runner = File.join(build_dir, "st", "run_all_tests")
    if $dry_run || File.exist?(test_runner)
      run_cmd("system tests -c", [test_runner, "-c"], log: systests_log)
    else
      $stderr.puts "  #{DIM}WARNING: test runner not found, skipping#{RESET}"
    end
  end

  # --- Main entry point ---
  #
  # Single loop per architecture:
  #   1. Wipe toolchain + install packages (once per arch)
  #   2. Default build (cmake + make + gtests)
  #   3. If --run-also-tilck-tests: run Tilck tests on the default build
  #   4. If --all-build-types: for each generator, build + optionally
  #      run Tilck tests — reusing the same installed packages
  #
  def run(run_tilck: false, all_build_types: false,
          arch: nil, packages_filter: nil)

    banner("System tests")

    builds_dir = MAIN_DIR / "other_builds"
    FileUtils.mkdir_p(builds_dir) if !$dry_run

    generators_dir = MAIN_DIR / "scripts" / "build_generators"
    generators = all_build_types ? Dir.children(generators_dir).sort : []

    for arch_name in resolve_archs(arch)
      banner("Architecture: #{arch_name}")

      # --- 1. Install packages (once per arch) ---
      wipe_toolchain
      install_packages(arch_name, packages_filter: packages_filter)

      # --- 2. Default build ---
      build_dir = (builds_dir / "systest_#{arch_name}").to_s
      prepare_build_dir(build_dir)

      Dir.chdir($dry_run ? "." : build_dir) do
        cmake_and_build(arch_name, build_dir)

        # --- 3. Tilck tests on the default build ---
        if run_tilck && TILCK_TEST_ARCHS.include?(arch_name)
          run_tilck_tests(arch_name, build_dir)
        end
      end

      # --- 4. All build generators (same installed packages) ---
      for gen_name in generators
        gen_script = (generators_dir / gen_name).to_s
        build_dir = (builds_dir / "#{gen_name}_#{arch_name}").to_s

        banner("#{gen_name} x #{arch_name}")
        prepare_build_dir(build_dir)

        Dir.chdir($dry_run ? "." : build_dir) do
          env = { "ARCH" => arch_name }

          cmake_log = File.join(build_dir, "cmake.log")
          step("generator #{gen_name}")

          if $dry_run
            dry
          else
            success = false
            elapsed = timed {
              success = system(env, gen_script,
                               out: cmake_log, err: cmake_log)
            }

            if !success || File.exist?("skipped")
              puts "#{DIM}skipped#{RESET}"
              next
            end
            ok(elapsed)
          end

          build_log = File.join(build_dir, "build.log")
          run_cmd("make -j", ["make", "-j"], log: build_log, env: env)

          if run_tilck && TILCK_TEST_ARCHS.include?(arch_name)
            run_tilck_tests(arch_name, build_dir)
          end
        end
      end
    end
  end

  def prepare_build_dir(build_dir)
    return if $dry_run
    FileUtils.rm_rf(build_dir)
    FileUtils.mkdir_p(build_dir)
  end
end
