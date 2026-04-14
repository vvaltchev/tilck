# SPDX-License-Identifier: BSD-2-Clause
#
# System tests: install all packages, build Tilck for all architectures,
# optionally run all build-generator configurations and Tilck's own tests.
#
# When $dry_run is set, the exact same code path executes but every
# action prints [ DRY ] instead of running. Timing works in both modes.
#

require 'pathname'
require 'fileutils'
require_relative '../term'

module SystemTests

  include Term
  module_function

  DRY_TAG = "#{CYAN256}[ DRY ]#{RESET}"

  # --- Paths ---

  MAIN_DIR   = Pathname.new(File.expand_path("../../..", __dir__))
  TC         = MAIN_DIR / "toolchain4"
  BTC        = (MAIN_DIR / "scripts" / "build_toolchain").to_s
  CMAKE_RUN  = (MAIN_DIR / "scripts" / "cmake_run").to_s
  BUILDS_DIR = MAIN_DIR / "other_builds"
  GEN_DIR    = MAIN_DIR / "scripts" / "build_generators"

  # --- Architecture constants ---

  ALL_TEST_ARCHS  = ["i386", "x86_64", "riscv64"]
  TILCK_TEST_ARCHS = ["i386", "riscv64"]
  DEFAULT_ARCH = ENV["ARCH"].to_s.empty? ? "i386" : ENV["ARCH"]

  # --- Package constants ---

  OPTIONAL_PACKAGES = %w[
    ncurses vim tcc fbdoom micropython lua treecmd tfblib
    gtest_src host_gtest lcov libmusl
  ]

  EXTRA_FLAG_MAP = {
    "EXTRA_VIM"          => ->(arch) { arch_pkg_dir(arch) / "vim" },
    "EXTRA_TCC"          => ->(arch) { arch_pkg_dir(arch) / "tcc" },
    "EXTRA_FBDOOM"       => ->(arch) { arch_pkg_dir(arch) / "fbdoom" },
    "EXTRA_MICROPYTHON"  => ->(arch) { arch_pkg_dir(arch) / "micropython" },
    "EXTRA_LUA"          => ->(arch) { arch_pkg_dir(arch) / "lua" },
    "EXTRA_TREE_CMD"     => ->(arch) { arch_pkg_dir(arch) / "treecmd" },
    "EXTRA_TFBLIB"       => ->(arch) { TC / "noarch" / "tfblib" },
  }

  # --- Path helpers ---

  def gcc_ver_for(arch_name)
    File.read(
      MAIN_DIR / "other" / "gcc_tc_conf" / arch_name / "default_ver"
    ).strip
  end

  def arch_pkg_dir(arch_name)
    TC / "gcc-#{gcc_ver_for(arch_name)}" / arch_name
  end

  def resolve_archs(arch)
    return ALL_TEST_ARCHS if arch == "ALL"
    return [arch] if arch
    [DEFAULT_ARCH]
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

  def wipe_toolchain
    step("Wipe toolchain (keep cache + Ruby)")

    if $dry_run
      return dry
    end

    t0 = now
    Dir.children(TC).each { |child|
      next if child == "cache" || child == "host"
      FileUtils.rm_rf(TC / child)
    }
    ok(now - t0)
  end

  def install_packages(arch_name, packages_filter: nil)
    env = { "ARCH" => arch_name, "BOARD" => nil }

    run_cmd("Install default packages for #{arch_name}",
            [BTC, "-q", "-n"], env: env)

    pkgs = OPTIONAL_PACKAGES
    if packages_filter
      re = Regexp.new(packages_filter)
      pkgs = pkgs.select { |p| p.match?(re) }
    end

    pkgs.each { |pkg|
      run_cmd("Install optional: #{pkg}",
              [BTC, "-q", "-n", "-s", pkg], env: env)
    }
  end

  def extra_cmake_flags(arch_name)
    EXTRA_FLAG_MAP.filter_map { |flag, path_proc|
      if $dry_run || (path_proc.call(arch_name).directory? rescue false)
        "-D#{flag}=1"
      end
    }
  end

  def cmake_and_build(arch_name, build_dir)
    env    = { "ARCH" => arch_name }
    extras = extra_cmake_flags(arch_name)
    desc   = extras.empty? ? "" : " " + extras.join(" ")

    run_cmd("cmake#{desc}", [CMAKE_RUN] + extras,
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

  def maybe_run_tilck_tests(arch_name, build_dir, run_tilck)
    if run_tilck && TILCK_TEST_ARCHS.include?(arch_name)
      run_tilck_tests(build_dir)
    end
  end

  # --- Per-arch phases ---

  def do_install(arch_name, packages_filter)
    section("Install packages") do
      wipe_toolchain
      install_packages(arch_name, packages_filter: packages_filter)
    end
  end

  def do_default_build(arch_name, run_tilck)
    build_dir = (BUILDS_DIR / "systest_#{arch_name}").to_s

    section("Default build") do
      in_build_dir(build_dir) do
        cmake_and_build(arch_name, build_dir)
        maybe_run_tilck_tests(arch_name, build_dir, run_tilck)
      end
    end
  end

  def do_generator_build(arch_name, gen_name, run_tilck)
    gen_script = (GEN_DIR / gen_name).to_s
    build_dir  = (BUILDS_DIR / "#{gen_name}_#{arch_name}").to_s
    env        = { "ARCH" => arch_name }

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

        maybe_run_tilck_tests(arch_name, build_dir, run_tilck)
      end
    end
  end

  def do_all_generators(arch_name, run_tilck)
    Dir.children(GEN_DIR).sort.each { |gen_name|
      do_generator_build(arch_name, gen_name, run_tilck)
    }
  end

  # --- Main entry point ---

  def run(run_tilck: false, all_build_types: false,
          arch: nil, packages_filter: nil)

    grand_t0 = now
    FileUtils.mkdir_p(BUILDS_DIR) if !$dry_run

    resolve_archs(arch).each { |arch_name|

      section("Architecture: #{arch_name}") do
        do_install(arch_name, packages_filter)
        do_default_build(arch_name, run_tilck)
        do_all_generators(arch_name, run_tilck) if all_build_types
      end
    }

    puts
    puts HLINE
    puts "  #{BOLD}Total system test time: " \
         "#{fmt_elapsed(now - grand_t0)}#{RESET}"
    puts HLINE
  end
end
