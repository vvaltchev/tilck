# SPDX-License-Identifier: BSD-2-Clause
#
# System tests: install all packages, build Tilck for all architectures,
# optionally run all build-generator configurations and Tilck's own tests.
#
# This module is called from run_all.rb AFTER unit tests pass.
# All package operations go through build_toolchain as a subprocess
# (clean PackageManager, no test pollution).
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
  # The pkgmgr will skip packages that don't support the arch.
  OPTIONAL_PACKAGES = %w[
    ncurses vim tcc fbdoom micropython lua treecmd tfblib
    gtest_src host_gtest lcov libmusl
  ]

  # Map of EXTRA_* cmake flags → the package dir to check.
  # Only enable a flag if the package is actually installed.
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
  #   nil     → [DEFAULT_ARCH]
  #   "ALL"   → ALL_TEST_ARCHS
  #   "i386"  → ["i386"]
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

  # Wipe installed packages from the toolchain, preserving:
  #   - cache/     (downloaded archives)
  #   - host/      (Ruby install + host tools rebuilt per arch anyway)
  #
  # We keep host/ because it contains the Ruby binary that
  # build_toolchain needs to exec into. Host tools (mtools, gtest)
  # will be reinstalled as part of the default package set.
  def wipe_toolchain
    step("Wipe toolchain (keep cache + Ruby)")
    elapsed = timed {
      Dir.children(TC).each { |child|
        next if child == "cache" || child == "host"
        FileUtils.rm_rf(TC / child)
      }
    }
    ok(elapsed)
  end

  def install_packages(arch_name, packages_filter: nil)
    step("Install default packages for #{arch_name}")
    env = { "ARCH" => arch_name, "BOARD" => nil }
    elapsed = timed {
      ok2 = system(env, BTC, "-q", "-n",
                   out: "/dev/null", err: "/dev/null")
      fail!("Default install failed for #{arch_name}") if !ok2
    }
    ok(elapsed)

    # Install optional packages (skip failures silently — some
    # packages don't support all archs or may lack cache files)
    pkgs = OPTIONAL_PACKAGES
    if packages_filter
      re = Regexp.new(packages_filter)
      pkgs = pkgs.select { |p| p.match?(re) }
    end

    for pkg in pkgs
      step("Install optional: #{pkg}")
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
  def extra_cmake_flags(arch_name)
    flags = []
    EXTRA_FLAG_MAP.each do |flag, path_proc|
      dir = path_proc.call(arch_name)
      if dir.directory? && !Dir.empty?(dir)
        flags << "-D#{flag}=1"
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

    run_cmd(
      "Running CMake",
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

    gtests_bin = File.join(build_dir, "gtests")
    if File.exist?(gtests_bin)
      run_cmd(
        "run gtests",
        [gtests_bin],
        log: gtests_log
      )
    end

    test_runner = File.join(build_dir, "st", "run_all_tests")
    if File.exist?(test_runner)
      run_cmd(
        "system tests -c",
        [test_runner, "-c"],
        log: systests_log
      )
    else
      $stderr.puts "  #{DIM}WARNING: test runner not found, skipping#{RESET}"
    end
  end

  # --- Main entry points ---

  def run_system_tests(run_tilck: false, arch: nil, packages_filter: nil)
    banner("System tests: install + build")

    builds_dir = MAIN_DIR / "other_builds"
    FileUtils.mkdir_p(builds_dir)

    for arch_name in resolve_archs(arch)
      banner("Architecture: #{arch_name}")

      wipe_toolchain
      install_packages(arch_name, packages_filter: packages_filter)

      build_dir = (builds_dir / "systest_#{arch_name}").to_s
      FileUtils.rm_rf(build_dir)
      FileUtils.mkdir_p(build_dir)

      Dir.chdir(build_dir) do
        cmake_and_build(arch_name, build_dir)

        if run_tilck && TILCK_TEST_ARCHS.include?(arch_name)
          run_tilck_tests(arch_name, build_dir)
        end
      end
    end
  end

  def run_all_build_types(run_tilck: false, arch: nil)
    banner("All build types x all architectures")

    builds_dir = MAIN_DIR / "other_builds"
    generators_dir = MAIN_DIR / "scripts" / "build_generators"
    generators = Dir.children(generators_dir).sort

    for arch_name in resolve_archs(arch)

      # Each arch needs its own toolchain — wipe and reinstall.
      banner("Install packages for #{arch_name}")
      wipe_toolchain
      install_packages(arch_name)

      for gen_name in generators
        gen_script = (generators_dir / gen_name).to_s
        build_dir = (builds_dir / "#{gen_name}_#{arch_name}").to_s

        banner("#{gen_name} x #{arch_name}")

        FileUtils.rm_rf(build_dir)
        FileUtils.mkdir_p(build_dir)

        Dir.chdir(build_dir) do
          env = { "ARCH" => arch_name }

          cmake_log = File.join(build_dir, "cmake.log")
          step("generator #{gen_name}")
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

          build_log = File.join(build_dir, "build.log")
          run_cmd("make -j", ["make", "-j"], log: build_log, env: env)

          if run_tilck && TILCK_TEST_ARCHS.include?(arch_name)
            run_tilck_tests(arch_name, build_dir)
          end
        end
      end
    end
  end
end
