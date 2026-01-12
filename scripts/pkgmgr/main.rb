# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'gcc'
require_relative 'cache'
require_relative 'progress'
require_relative 'package_manager'

require 'pathname'
require 'fileutils'
require 'optparse'

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
      arch.target_dir = TC / ver._ / name
      arch.host_dir = TC / ver._ / "host_#{name}"
      arch.host_syscc_dir = TC / "syscc" / "host_#{name}"
    end
  end

  def check_gcc_tc_ver

    failures = 0
    for name, arch in ALL_ARCHS do

      v = arch.gcc_ver
      min = arch.min_gcc_ver

      if v && v < min
        puts "ERROR: [arch #{name}] gcc ver #{v} < required #{min}"
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
      puts "ERROR: Tilck must be checked out in a path *WITHOUT* spaces"
      puts "Project's root dir: '#{MAIN_DIR}'"
      exit 1
    end
    if BOARD && !BOARD_BSP.exist?
      puts "ERROR: BOARD_BSP: #{BOARD_BSP} not found!"
      exit 1
    end
  end

  def create_toolchain_dirs
    for name, arch in ALL_ARCHS do
      mkdir_p(TC / arch.gcc_ver._ / name)
    end
    for compiler in [ HOST_ARCH.gcc_ver._, "syscc" ] do
      mkdir_p(TC / compiler / "host_#{HOST_ARCH.name}")
    end
  end

  def parse_options

    is_option = ->(line) { line.lstrip.start_with?("-") }
    add_vertical_space = ->(summary) {
      blocks = []
      curr = []
      summary.each { |line|
        if is_option.(line) && !curr.empty?
          blocks << curr; curr = []
        end
        curr << line
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
      install: [],
      install_compiler: [],
      uninstall: [],
      uninstall_compiler: [],
      arch: nil,
      compiler: nil,
      group_by: nil,
    }

    mode_opts = [
      :help,
      :just_context,
      :list,
      :install,
      :install_compiler,
      :uninstall,
      :uninstall_compiler,
    ]

    argv = ARGV.dup()

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
      puts add_vertical_space.call(p.summarize())
    }

    p.on('-l', '--list', 'List all packages status [MODE]') {
      opts[:list] = true
    }

    p.on('-j', '--just-context', 'Just show the context and quit [MODE]') {
      opts[:just_context] = true
    }

    p.on('-d', '--dry-run', 'Dry run for the uninstall mode') {
      opts[:dry_run] = true
    }

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
      '-f', '--force',
      'Force. Meaning depending on the MODE. In uninstall mode, this includes',
      'the cross-compilers, when the package name is ALL.'
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
        pkgmgr.build_gcc_package_name(arch_obj, "musl") + ":#{ver}"
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

    puts "Context"
    puts "------------------"
    dump_context

    puts
    puts
    options = parse_options()

    if options[:help]
      return 0
    end

    if options[:just_context]
      return 0
    end

    pkgmgr.refresh()

    if options[:list]
      pkgmgr.show_status_all(
        options[:group_by],
        options[:compiler].eql?("ALL")
      )
      return 0
    end

    if !options[:install].blank?
      # First, check that all packages exist so that we don't install some
      # package and fail because of others, leaving in an undesired mid-state.
      for name in options[:install] do
        name, ver = name.split(":")
        if !pkgmgr.get(name)
          puts "ERROR: package not found: #{name}"
          return 1
        end
       end

      # Now install the packages. We could still fail, but at least we know
      # about all the packages mentioned by the user.
      for name in options[:install] do
        name, v = name.split(":")
        if !pkgmgr.install(name, Ver(v))
          puts "ERROR: couldn't install: #{name}"
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

    return 0
  end
end

if __FILE__ == $0
  exit Main::main(ARGV)
end
