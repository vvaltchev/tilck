# SPDX-License-Identifier: BSD-2-Clause

# Version check must happen before anything else to prevent confusing
# errors on old Ruby. The check is in version_check.rb, loaded by
# arch.rb, loaded by early_logic.rb.
require_relative 'version_check'

# When COVERAGE_DIR is set (by the test runner), collect line coverage
# for this process and write it to a JSON file on exit. This allows
# merging coverage from subprocess installs with the main test run.
# Skip if Coverage is already running (e.g. when loaded by the test
# runner process itself).
if ENV["COVERAGE_DIR"] && !(defined?(Coverage) && Coverage.running?)
  require 'coverage'
  require 'json'
  Coverage.start(lines: true)
  at_exit {
    dir = ENV["COVERAGE_DIR"]
    FileUtils.mkdir_p(dir) rescue nil
    path = File.join(dir, "coverage_#{Process.pid}.json")
    File.write(path, JSON.generate(Coverage.result))
  }
end

require_relative 'early_logic'
require_relative 'arch'
require_relative 'term'
require_relative 'version'
require_relative 'package'
require_relative 'gcc'
require_relative 'cache'
require_relative 'progress'
require_relative 'package_manager'
require_relative 'zlib'
require_relative 'acpica'
require_relative 'mtools'
require_relative 'busybox'
require_relative 'gnuefi'
require_relative 'gtest'
require_relative 'ncurses'
require_relative 'dtc'
require_relative 'uboot'
require_relative 'sophgo_tools'
require_relative 'licheerv_nano_boot'
require_relative 'lua'
require_relative 'fbdoom'
require_relative 'treecmd'
require_relative 'lcov'
require_relative 'libmusl'
require_relative 'micropython'
require_relative 'tcc'
require_relative 'vim'
require_relative 'tfblib'

require 'pathname'
require 'fileutils'
require 'optparse'
require 'rbconfig'

module Main

  extend FileShortcuts
  extend FileUtilsShortcuts
  module_function

  def read_gcc_ver_defaults
    conf = MAIN_DIR / "other" / "gcc_tc_conf"
    for name, arch in ALL_ARCHS do
      arch.min_gcc_ver = Ver(File.read(conf / name / "min_ver"))
      arch.default_gcc_ver = Ver(File.read(conf / name / "default_ver"))
      arch.gcc_ver = arch.default_gcc_ver
    end
  end

  def set_gcc_tc_ver

    ver = Ver(getenv("GCC_TC_VER", ARCH.default_gcc_ver))
    ALL_ARCHS[ARCH.name].gcc_ver = ver

    if ARCH.family == "generic_x86"
       # Special case for x86: since we're downloading both toolchains
       # also to be used for Tilck (bootloader), not just for the host
       # apps, it makes sense to force GCC_TC_VER to also apply for the
       # other architecture. In general case (e.g. riscv64, aarch64) that
       # won't happen, as we need only *one* GCC toolchain for Tilck and
       # one for the host apps.
      ALL_ARCHS["i386"].gcc_ver = ver
      ALL_ARCHS["x86_64"].gcc_ver = ver
    end

    for name, arch in ALL_ARCHS do
      arch.target_dir = TC / "gcc-#{arch.gcc_ver}" / name
    end
  end

  def check_gcc_tc_ver

    failures = 0
    for name, arch in ALL_ARCHS do

      v = arch.gcc_ver
      min = arch.min_gcc_ver

      if v && v < min
        error "[arch #{name}] gcc ver #{v} < required #{min}"
        failures += 1
      end
    end

    if failures > 0
      puts
      puts "Steps to fix:"
      puts
      puts "   1. unset \$GCC_TC_VER"
      puts "   2. ./scripts/build_toolchain --clean"
      puts "   3. rm -rf build # or any other build directory"
      puts "   4. ./scripts/build_toolchain"
      puts
      exit 1
    end
  end

  def dump_context

    de = ->(x) {
      (x.start_with? "ENV:") ? ENV[x[4..]] : Object.const_get(x).to_s
    }

    list = %w[
      ENV:GCC_TC_VER
      ENV:CC
      ENV:CXX
      ENV:ARCH
      ENV:BOARD
      MAIN_DIR
      TC
      HOST_ARCH
      HOST_OS
      HOST_DISTRO
      HOST_CC
      ARCH
      BOARD
      DEFAULT_BOARD
    ]

    list.each { |x| puts "#{x} = #{de.call(x)}" }
    for k, v in ALL_ARCHS do
      puts "GCC_VER[#{k}]: #{v.gcc_ver}"
    end
  end

  def early_checks
    if !(MAIN_DIR.to_s.index ' ').nil?
      error "Tilck must be checked out in a path *WITHOUT* spaces"
      puts "Project's root dir: '#{MAIN_DIR}'"
      exit 1
    end
    if BOARD && !BOARD_BSP.exist?
      error "BOARD_BSP: #{BOARD_BSP} not found!"
      exit 1
    end
  end

  def create_toolchain_dirs
    for name, arch in ALL_ARCHS do
      mkdir_p(TC / "gcc-#{arch.gcc_ver}" / name)
    end
    mkdir_p(HOST_DIR)
    mkdir_p(HOST_DIR_PORTABLE)
    mkdir_p(TC_NOARCH)
  end

  def parse_options(argv = ARGV.dup)

    is_option = ->(line) { line.lstrip.start_with?("-") }
    highlight = ->(line) {
      return line if not STDOUT.tty?
      line.sub!("[MODE]", "[#{Term.makeGreen("MODE")}]")
      line.sub!("[FLAG]", "[#{Term.makeYellow("FLAG")}]")
      line.sub!("[OPTION]", "[#{Term.makeYellow("OPTION")}]")
      line.sub!("ALL", Term.makeRed("ALL"))
      line
    }
    reformat_summary = ->(summary) {
      blocks = []
      curr = []
      summary.each { |line|
        if is_option.(line) && !curr.empty?
          blocks << curr; curr = []
        end
        curr << highlight.call(line)
      }
      blocks << curr unless curr.empty?
      blocks.map { |b| b.join }.join("\n") + "\n"
    }

    opts = {
      help: false,
      skip_install_pkgs: false,
      just_context: false,
      dry_run: false,
      list: false,
      force: false,
      self_test: false,
      coverage: false,
      system_tests: false,
      all_build_types: false,
      run_tilck_tests: false,
      check_for_updates: false,
      upgrade: false,
      config: nil,
      install: [],
      install_compiler: [],
      uninstall: [],
      uninstall_compiler: [],
      arch: nil,
      compiler: nil,
      group_by: nil,
      quiet: 0,
    }

    mode_opts = [
      :help,
      :just_context,
      :list,
      :self_test,
      :check_for_updates,
      :upgrade,
      :config,
      :install,
      :install_compiler,
      :uninstall,
      :uninstall_compiler,
    ]

    get_multiple_args = ->(first, sym) {
      list = [first]
      while argv.first && argv.first !~ /\A-/
        list << argv.shift
      end
      opts[sym] += list
    }

    p = OptionParser.new('./scripts/build_toolchain [-n] [OPTIONS]')

    p.on('-h', '--help', 'Show this help message [MODE]') {
      opts[:help] = true
      puts p.banner
      puts
      puts reformat_summary.call(p.summarize())
    }

    p.on('-l', '--list', 'List all packages status [MODE]') {
      opts[:list] = true
    }

    p.on('-j', '--just-context', 'Just show the context and quit [MODE]') {
      opts[:just_context] = true
    }

    p.on('-t', '--self-test', 'Run internal unit tests [MODE]') {
      opts[:self_test] = true
    }

    p.on('--coverage',
         'Collect code coverage data + HTML report (use with -t) [FLAG]') {
      opts[:coverage] = true
    }

    p.on('--system-tests',
         'After unit tests: install all pkgs, build for all archs [FLAG]') {
      opts[:system_tests] = true
    }

    p.on('--all-build-types',
         'With --system-tests: build all generator configs too [FLAG]') {
      opts[:all_build_types] = true
    }

    p.on('--run-also-tilck-tests',
         'With --system-tests: run gtests + system tests (i386/riscv64) [FLAG]') {
      opts[:run_tilck_tests] = true
    }

    p.on('-F', '--filter REGEX',
         'Run only tests matching REGEX (use with -t) [OPTION]') {
      |pat| (opts[:test_args] ||= []) << "--filter" << pat
    }

    p.on('-V', '--verbose-tests',
         'Show stdout/stderr even for passing tests (use with -t) [FLAG]') {
      (opts[:test_args] ||= []) << "--verbose-tests"
    }

    p.on('--test-packages-filter REGEX',
         'With --system-tests: install only optional packages matching REGEX') {
      |pat| (opts[:test_args] ||= []) << "--test-packages-filter" << pat
    }

    p.on(
      '-C', '--config PKG',
      'Reconfigure a package interactively (e.g. make menuconfig) [MODE]'
    ) { |pkg| opts[:config] = pkg }

    p.on(
      '--upgrade',
      'Upgrade installed packages whose version was bumped in',
      'pkg_versions. Does not install new packages. [MODE]'
    ) { opts[:upgrade] = true }

    p.on(
      '--check-for-updates',
      'Check if any installed packages need upgrading. Prints nothing',
      'and exits 0 if up to date, or prints the list and exits 2 if',
      'upgrades are needed. Lightweight: meant to be called directly',
      'by CMake without the bash wrapper. [MODE]'
    ) { opts[:check_for_updates] = true }

    p.on('-s', '--install PKG', 'Install the given package [MODE]') do |first|
      get_multiple_args.call(first, :install)
    end

    p.on(
      '-S', '--install-compiler ARCH',
      'Install a GCC + libmusl cross-compiler for the given ARCH [MODE]'
    ) do |first|
      get_multiple_args.call(first, :install_compiler)
    end

    p.on(
      '-u', '--uninstall PKG[:VER]',
      'Uninstall the given version (optional) of a package [MODE]'
    ) do |first|
      get_multiple_args.call(first, :uninstall)
    end

    p.on(
      '-U', '--uninstall-compiler ARCH',
      'Uninstall the GCC + libmusl cross-compiler for the given ARCH [MODE]'
    ) do |first|
      get_multiple_args.call(first, :uninstall_compiler)
    end

    p.on('-d', '--dry-run', 'Dry run for the uninstall mode [FLAG]') {
      opts[:dry_run] = true
    }

    p.on('-g', '--group-by WHAT', ['ver', 'arch'],
         'Group packages by "ver" or "arch" [OPTION]') { |what|
      opts[:group_by] = what
    }

    p.on(
      '-c', '--compiler-ver VER',
      'Make the uninstall operation affect only packages built by the given',
      'compiler version. The special value ALL, means all compilers. The',
      'special value "syscc" means the system compiler. Using that makes',
      'sense only for host packages like the GCC toolchains themselves and',
      'other build host tools [OPTION]'
    ) do |value|

      if value != "ALL" and value != "syscc"
        Ver(value) # check that the version can be parsed
      end

      opts[:compiler] = value
    end

    p.on(
      '-a', '--arch ARCH',
      'Make the uninstall operation affect only packages built for the given',
      'architecture. The special value ALL, means all architectures. [OPTION]'
    ) do |value|

      if value != "ALL"
        if !ALL_ARCHS.include? value
          raise OptionParser::InvalidArgument, "Unknown architecture: #{value}"
        end
      end

      opts[:arch] = value
    end

    p.on(
      '-q', 'Be quiet: skip the bootstrap logging [FLAG]'
    ) { opts[:quiet] = 1 }

    p.on(
      '-f', '--force',
      'Force. Meaning depending on the MODE. In uninstall mode, this includes',
      'the cross-compilers, when the package name is ALL. [FLAG]'
    ) { opts[:force] = true }

    p.on(
      '-n', '--skip-install-pkgs',
      'Do not check/install system dependencies. This flag is useful when the',
      'user run at least *one* time this script without this flag so that the',
      'necessary packages have been installed and the system configuration nor',
      'the dependencies in the source have changed since then. Using this flag',
      'improves the speed, but it is generally discouraged, unless this script',
      'is run on a *unsupported* Linux distribution or the user is experienced',
      'with Tilck\'s package manager and prepared to handle a failure. [FLAG]'
    ) { opts[:skip_install_pkgs] = true }

    p.parse!(argv)
    mods = opts.slice(*mode_opts)
    mods = mods.select { |k,v| !v.blank? }

    if mods.length > 1
      raise OptionParser::InvalidArgument,
            "Cannot use more than one mode options"
    end

    if opts[:list] and (!opts[:compiler].nil? and !opts[:compiler].eql?("ALL"))
      raise OptionParser::InvalidArgument, "with -l only -c ALL can be used"
    end

    for dest, source in [
      [:install,:install_compiler],
      [:uninstall,:uninstall_compiler]
    ] do
      opts[dest] += opts[source].map { |x|
        arch, ver = x.split(":")
        arch_obj = ALL_ARCHS[arch]
        if !arch_obj
          raise OptionParser::InvalidArgument, "Unknown architecture: #{arch}"
        end
        "gcc-#{arch_obj.name}-musl:#{ver}"
      }
    end
    return opts
  end

  def main(argv)

    early_checks
    read_gcc_ver_defaults
    set_gcc_tc_ver
    check_gcc_tc_ver
    create_toolchain_dirs

    if ENV['QUIET'].blank? or ENV['QUIET'] == '0'
      puts "Context"
      puts "------------------"
      dump_context
      puts
      puts
    end

    options = parse_options(argv)

    if options[:help]
      return 0
    end

    if options[:just_context]
      return 0
    end

    if options[:self_test]
      runner = File.join(__dir__, "tests", "run_all.rb")
      args = [runner]
      args << "--coverage" if options[:coverage]
      args << "--dry-run" if options[:dry_run]
      args << "--system-tests" if options[:system_tests]
      args << "--all-build-types" if options[:all_build_types]
      args << "--run-also-tilck-tests" if options[:run_tilck_tests]
      args << "--test-arch" << options[:arch] if options[:arch]
      args += options[:test_args] if options[:test_args]
      # exec into a fresh Ruby process so Coverage.start runs before
      # any pkgmgr modules are loaded (coverage only tracks files
      # loaded after start).
      exec(RbConfig.ruby, *args)
    end

    if options[:check_for_updates]
      pkgmgr.refresh()
      upgrades = pkgmgr.get_upgradable_packages
      if upgrades.empty?
        return 0
      end
      names = upgrades.map(&:name).sort
      puts "NEEDS_UPGRADE #{names.join(' ')}"
      return 2
    end

    pkgmgr.refresh()

    begin
      pkgmgr.validate_deps
    rescue DepResolver::CycleError, DepResolver::MissingDepError => e
      error "Dependency graph error: #{e.message}"
      return 1
    end

    if options[:list]
      pkgmgr.show_status_all(
        options[:group_by],
        options[:compiler].eql?("ALL")
      )
      return 0
    end

    if options[:upgrade]
      upgrades = pkgmgr.get_upgradable_packages
      if upgrades.empty?
        info "All installed packages are up to date"
        return 0
      end

      plan = pkgmgr.resolve_install_plan(
        upgrades.map { |p| [p.name, nil] }
      )

      info "Packages to upgrade: #{plan.map(&:first).join(', ')}"
      for name, ver in plan do
        if !pkgmgr.install(name, ver)
          error "Could not install: #{name}"
          return 1
        end
      end
      return 0
    end

    if options[:config]
      pkg = pkgmgr.get(options[:config])
      if !pkg
        error "Package not found: #{options[:config]}"
        return 1
      end
      if !pkg.configurable?
        error "Package #{pkg.name} does not support reconfiguration"
        return 1
      end
      return pkg.configure() ? 0 : 1
    end

    if !options[:install].blank?

      # Parse "name:ver" pairs, validating that each package exists.
      requested = options[:install].map { |s|
        name, ver = s.split(":")
        if !pkgmgr.get(name)
          error "Package not found: #{name}"
          return 1
        end
        [name, Ver(ver)]
      }

      # Resolve the full install plan: transitive deps, minus
      # already-installed, in topological order.
      plan = pkgmgr.resolve_install_plan(requested)

      if plan.empty?
        info "All requested packages are already installed"
        return 0
      end

      # Show the plan (like APT), then proceed without confirmation.
      dep_names = plan.map(&:first) - requested.map(&:first)
      if !dep_names.empty?
        info "Dependencies to install: #{dep_names.join(', ')}"
      end
      info "Install order: #{plan.map(&:first).join(' -> ')}"

      for name, ver in plan do
        if !pkgmgr.install(name, ver)
          error "Could not install: #{name}"
          return 1
        end
      end
      return 0
    end

    if !options[:uninstall].blank?
      for name in options[:uninstall] do
        name, v = name.split(":")
        pkgmgr.uninstall(
          name,
          options[:dry_run],
          options[:force],
          v == 'ALL' ? v : Ver(v),
          options[:compiler],
          options[:arch],
        )
      end
      return 0
    end

    # No mode flag specified: install default packages AND upgrade any
    # installed packages whose version was bumped in pkg_versions.
    defaults = pkgmgr.get_default_packages
    upgrades = pkgmgr.get_upgradable_packages
    all = (defaults + upgrades).uniq(&:name)

    plan = pkgmgr.resolve_install_plan(
      all.map { |p| [p.name, nil] }
    )

    if plan.empty?
      info "All default packages are installed and up to date"
      return 0
    end

    upgrade_names = upgrades.map(&:name) & plan.map(&:first)
    if !upgrade_names.empty?
      info "Packages to upgrade: #{upgrade_names.join(', ')}"
    end

    dep_names = plan.map(&:first) - all.map(&:name)
    if !dep_names.empty?
      info "Dependencies to install: #{dep_names.join(', ')}"
    end
    info "Install order: #{plan.map(&:first).join(' -> ')}"

    for name, ver in plan do
      if !pkgmgr.install(name, ver)
        error "Could not install: #{name}"
        return 1
      end
    end

    return 0
  end # method main()
end # module Main

if __FILE__ == $0
  exit Main::main(ARGV)
end
