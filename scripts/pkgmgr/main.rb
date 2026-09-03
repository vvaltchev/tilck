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
require_relative 'system_pkgs'
require_relative 'system_deps'
require_relative 'gcc'
require_relative 'cache'
require_relative 'progress'
require_relative 'package_manager'
require_relative 'layout'
require_relative 'zlib'
require_relative 'acpica'
require_relative 'mtools'
require_relative 'busybox'
require_relative 'mconf'
require_relative 'binutils'
require_relative 'linux_headers'
require_relative 'glibc'
require_relative 'gcc_prereqs'
require_relative 'host_gcc'
require_relative 'host_zlib'
require_relative 'ninja'
require_relative 'meson'
require_relative 'pixman'
require_relative 'libffi'
require_relative 'pcre2'
require_relative 'glib2'
require_relative 'expat'
require_relative 'fribidi'
require_relative 'libpng'
require_relative 'freetype'
require_relative 'harfbuzz'
require_relative 'fontconfig'
require_relative 'x11'
require_relative 'cairo'
require_relative 'gdk_pixbuf'
require_relative 'xkbcommon'
require_relative 'pango'
require_relative 'libepoxy'
require_relative 'libseccomp'
require_relative 'librsvg'
require_relative 'glycin'
require_relative 'libxml2'
require_relative 'dbus'
require_relative 'at_spi2_core'
require_relative 'gtk3'
require_relative 'host_python'
require_relative 'qemu'
require_relative 'gnuefi'
require_relative 'gtest'
require_relative 'ncurses'
require_relative 'dtc'
require_relative 'uboot'
require_relative 'sophgo_tools'
require_relative 'licheerv_nano_boot'
require_relative 'lua'
require_relative 'freedoom'
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

  # Packages added on top of the normal default set when the user
  # passes --contrib to `build_toolchain`. Intended for contributors
  # — tools that aren't required to build or run Tilck, but make dev
  # work smoother (e.g. host_mconf, which powers run_config).
  # host_mconf transitively depends on host_ncurses via the package's
  # dep_list, so the latter is pulled in automatically by the dep
  # resolver.
  CONTRIB_EXTRA_PACKAGES = %w[host_mconf].freeze

  def read_gcc_ver_defaults
    conf = MAIN_DIR / "other" / "gcc_tc_conf"
    for name, arch in ALL_ARCHS do
      arch.min_gcc_ver = Ver(File.read(conf / name / "min_ver"))
      arch.default_gcc_ver = Ver(File.read(conf / name / "default_ver"))
      arch.gcc_ver = arch.default_gcc_ver
    end
  end

  # Resolve user-supplied package name (possibly a substring) to a full
  # registered package name. On success returns the full name, logging
  # a short->full translation if one happened. On failure prints an
  # error message and returns nil — the caller should exit non-zero.
  def resolve_pkg_name(input)
    full, matches = pkgmgr.resolve_name(input)
    return full if full && full == input

    if full
      info "Matched '#{input}' -> '#{full}'"
      return full
    end

    if matches.empty?
      error "Package not found: #{input}"
      return nil
    end

    shown = matches.first(3).join(", ")
    suffix = matches.length > 3 ? ", ..." : ""
    error "Ambiguous package name '#{input}' matches: #{shown}#{suffix}"
    return nil
  end

  # -----------------------------------------------------------
  # Dependency tree renderer — used by the install plan display
  # and by the --deps introspection mode.
  # -----------------------------------------------------------

  # Render a dependency visualization for `roots`.
  #
  #   roots          — Array of package name strings (top-level).
  #   graph          — { name => [dep_name, ...] } from build_dep_graph.
  #   installed      — Set of package names that are already installed.
  #   show_installed — false: omit installed deps (install-plan mode).
  #                    true:  show them in dim/gray (--deps mode).
  #   ascii          — false: tree(1)-style box-drawing characters
  #                           with extra vertical spacing.
  #                    true:  plain-text 2-space indentation, one
  #                           line per node, no decoration. Machine-
  #                           friendly for parsing and tests.
  #
  # Returns an Array of ready-to-puts strings.
  def render_dep_trees(roots, graph, installed: Set.new,
                       show_installed: false, ascii: false)
    lines = []
    roots.each_with_index do |name, ri|
      lines << "" if ri > 0 && !ascii
      if ascii
        dep_tree_ascii(name, graph, installed, show_installed,
                       lines, "", Set.new)
      else
        dep_tree_root(name, graph, installed, show_installed, lines)
      end
    end
    lines
  end

  # --- ASCII (machine-friendly) mode ---

  def dep_tree_ascii(name, graph, installed, show_installed,
                     lines, indent, visited)
    lines << "#{indent}#{name}"
    return if visited.include?(name)
    deps = dep_tree_deps(name, graph, installed, show_installed)
    new_visited = visited | [name]
    deps.each do |dep|
      dep_tree_ascii(dep, graph, installed, show_installed,
                     lines, indent + "  ", new_visited)
    end
  end

  # --- Fancy (human-friendly) mode ---
  #
  # Standard tree(1) geometry (K=4 cols per level) with two tweaks:
  #   1. A 4-space leading indent before the whole tree.
  #   2. The root uses a bare "┌ name" corner (no dash), so it sits
  #      at the same column as the level-2 connectors — like tree(1)
  #      does with the plain root name.
  #
  # Every subtree gets an extra trunk-only "…│" line before its
  # first child and between siblings, so the vertical "│" connector
  # is always visible (even when a subtree has only one child).

  LEAD = "    "

  def dep_tree_root(name, graph, installed, show_installed, lines)
    deps = dep_tree_deps(name, graph, installed, show_installed)

    # Root with no visible subtree uses a bare "─ " bullet rather
    # than the "┌ " corner — there is no trunk to open.
    corner = deps.empty? ? "─" : "┌"
    lines << "#{LEAD}#{corner} #{dep_tree_fmt(name, installed, show_installed)}"

    if deps.empty?
      if show_installed
        lines << "#{LEAD}(no dependencies)"
      end
      return
    end

    spacer = "#{LEAD}│"
    lines << spacer
    deps.each_with_index do |dep, i|
      last = (i == deps.length - 1)
      dep_tree_child(dep, graph, LEAD, last, lines, installed,
                     show_installed, Set.new([name]))
      lines << spacer if !last
    end
  end

  def dep_tree_child(name, graph, prefix, is_last, lines, installed,
                     show_installed, visited)
    conn = is_last ? "└── " : "├── "
    lines << "#{prefix}#{conn}#{dep_tree_fmt(name, installed, show_installed)}"

    return if visited.include?(name)
    deps = dep_tree_deps(name, graph, installed, show_installed)
    return if deps.empty?

    child_prefix = prefix + (is_last ? "    " : "│   ")
    spacer = "#{child_prefix}│"
    lines << spacer

    new_visited = visited | [name]
    deps.each_with_index do |dep, i|
      last = (i == deps.length - 1)
      dep_tree_child(dep, graph, child_prefix, last, lines, installed,
                     show_installed, new_visited)
      lines << spacer if !last
    end
  end

  # --- Shared helpers ---

  def dep_tree_deps(name, graph, installed, show_installed)
    deps = graph[name] || []
    show_installed ? deps : deps.reject { |d| installed.include?(d) }
  end

  def dep_tree_fmt(name, installed, show_installed)
    if show_installed && installed.include?(name)
      "#{Term::DIM}#{name}#{Term::RESET}"
    else
      name
    end
  end

  # -----------------------------------------------------------

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

    # Where this arch's packages live: the pkgs/ directory of its
    # stack. Kept on the Architecture because a few packages need a
    # sibling package's install path (vim needs ncurses').
    for name, arch in ALL_ARCHS do
      arch.target_dir = Coords.new(
        "tilck-#{name}", arch.default_board, "gcc-#{arch.gcc_ver}"
      ).pkgs_dir
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

    # Not a constant: -H moves it, and a run that built into another
    # stack should say so where every other coordinate is printed.
    puts "HOST_STACK = gcc-#{pkgmgr.current_host_stack}"

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
    # A BSP is board DATA -- device tree, u-boot config -- and only the
    # embedded targets have any. Every arch now names a board, because
    # the board is part of an installed package's path, but x86's is
    # just "pc" and carries no BSP. So a missing BSP is an error only
    # for an arch that ships them at all, which still catches a
    # misspelled BOARD on riscv64.
    bsp_root = MAIN_DIR / "other" / "bsp" / ARCH.name

    if BOARD && bsp_root.directory? && !board_bsp.exist?
      error "BOARD_BSP: #{board_bsp} not found!"
      exit 1
    end
  end

  def create_toolchain_dirs
    for name, arch in ALL_ARCHS do
      mkdir_p(arch.target_dir)
    end
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
      list_installable: false,
      deps: [],
      ascii: false,
      force: false,
      self_test: false,
      coverage: false,
      system_tests: false,
      all_build_types: false,
      run_tilck_tests: false,
      check_for_updates: false,
      clean: false,
      print_layout: false,
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
      :list_installable,
      :deps,
      :self_test,
      :check_for_updates,
      :clean,
      :list_stacks,
      :print_layout,
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

    p.on('-L', '--list-stacks',
         'List the host stacks: which compilers a world has been',
         'built with, and how many packages are in each [MODE]') {
      opts[:list_stacks] = true
    }

    p.on('-H', '--host-gcc STACK',
         'Build for, and look at, the stack of the given host GCC',
         'instead of the one HOST_VER_GCC names. Applies to every',
         'mode: -s builds into that stack, -l and -L report on it.',
         'Written either way: "gcc-14.4.0", as -L prints it, or the',
         'bare "14.4.0". The stack does not have to exist yet --',
         'asking for it is what builds it. [OPTION]') { |v|
      opts[:host_gcc] = v
    }

    p.on('--list-installable',
         'Print package names installable via -s for the current ARCH,',
         'one per line, no decoration. Machine-readable output for',
         'tooling (e.g. system tests that need to filter per-arch',
         'supported packages) [MODE]') {
      opts[:list_installable] = true
    }

    p.on('-D', '--deps PKG',
         'Show the dependency tree for the given package(s).',
         'Already-installed deps are shown in gray. Respects',
         '-a <arch> for cross-arch queries. [MODE]') do |first|
      get_multiple_args.call(first, :deps)
    end

    p.on('--ascii',
         'Use plain-text indented output for dependency trees.',
         'Machine-friendly alternative to the fancy box-drawing',
         'format. Applies to --deps, -s install plans, and the',
         'default-install plan. [FLAG]') {
      opts[:ascii] = true
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
      '-C', '--config PKG[:VER]',
      'Reconfigure the given version (optional) of a package',
      'interactively (e.g. make menuconfig) [MODE]'
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

    p.on(
      '--clean',
      'Uninstall everything, keeping the prebuilt cross-compilers, the',
      'bootstrap Ruby and the download cache. What is left is what a',
      'fresh checkout would download anyway, so the rebuild after it',
      'is a real one. Combine with -d to see what would go. [MODE]'
    ) { opts[:clean] = true }

    p.on(
      '--print-layout',
      'Print the installed-package directories as KEY=value lines, so',
      'that the build system does not have to reconstruct them from',
      'the layout schema. Reads ARCH, BOARD and GCC_TC_VER from the',
      'environment like every other mode. Lightweight: meant to be',
      'called directly by CMake without the bash wrapper. [MODE]'
    ) { opts[:print_layout] = true }

    p.on('-s', '--install PKG',
         'Install the given package. Use ALL to install every',
         'installable non-compiler package for the current ARCH',
         '(compilers are auto-pulled in as deps). [MODE]') do |first|
      get_multiple_args.call(first, :install)
    end

    p.on(
      '-S', '--install-compiler ARCH',
      'Install a GCC + libmusl cross-compiler for the given ARCH.',
      'Use ALL to install every registered cross-compiler. [MODE]'
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
      'Uninstall the GCC + libmusl cross-compiler for the given ARCH.',
      'Use ALL to uninstall every registered cross-compiler. [MODE]'
    ) do |first|
      get_multiple_args.call(first, :uninstall_compiler)
    end

    p.on('-d', '--dry-run',
         'Dry run: show what would be done and exit without touching',
         'the filesystem. Applies to -s, -S, -u, -U. [FLAG]') {
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
      'Target architecture for the current operation. In install mode',
      '(-s), sets the architecture to build packages for (overrides',
      'ARCH=). In uninstall mode (-u), filters to installations of',
      'that architecture. The special value ALL means all architectures.',
      '[OPTION]'
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
      'Force. Meaning depends on the MODE. In uninstall mode, this includes',
      'the cross-compilers, when the package name is ALL. In install mode',
      '(-s), this forces an uninstall+install cycle for each requested',
      'package even if already installed. [FLAG]'
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

    p.on(
      '--contrib',
      'When combined with the default install (no mode flag), also',
      'install packages useful for contributors: host_mconf',
      '(plus its host_ncurses dep). Intended to be run once, like:',
      './scripts/build_toolchain --contrib. Packages listed in',
      'CONTRIB_EXTRA_PACKAGES are appended to the normal default',
      'set before the plan is resolved. [FLAG]'
    ) { opts[:contrib] = true }

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
      opts[dest] += opts[source].flat_map { |x|
        arch, ver = x.split(":")
        # ALL: every registered cross-compiler.
        if arch == "ALL"
          next ALL_ARCHS.values.map { |a|
            "gcc-#{a.name}-musl:#{ver}"
          }
        end
        arch_obj = ALL_ARCHS[arch]
        if !arch_obj
          raise OptionParser::InvalidArgument, "Unknown architecture: #{arch}"
        end
        ["gcc-#{arch_obj.name}-musl:#{ver}"]
      }
    end

    # NOTE: -s ALL expansion moved to main(), inside the
    # with_target_arch scope, so it respects -a <arch>.

    return opts
  end

  # Expand "ALL" entries in `install_list` into every installable
  # non-compiler package for the pkgmgr's current target_arch. Called
  # inside the with_target_arch scope so arch_supported? sees the
  # right arch. Compilers are reached via -S ALL or as implicit deps.
  def expand_install_all(install_list)
    install_list.flat_map { |x|
      raw, ver = x.split(":")
      next [x] unless raw == "ALL"
      pkgmgr.all_packages
            .reject(&:is_compiler)
            .reject { |p| p.get_installable_list.empty? }
            .map { |p| "#{p.name}:#{ver}" }
    }
  end

  # -H names a stack, either as it is spelled everywhere else --
  # "gcc-14.4.0", which is what -L prints and what the path holds --
  # or as the bare version that names it just as unambiguously.
  #
  # It does not have to be built yet: asking for a stack is how it
  # gets built. It does have to be one the compiler package knows how
  # to build, or the whole run would go to coordinates nothing can
  # ever fill.
  def select_host_stack(str)

    gcc = pkgmgr.stack_compiler
    ver = Coords.parse_stack(str)

    # Nil only when no compiler package is registered at all, which
    # is a broken registry rather than a user error -- but saying so
    # beats a NoMethodError from inside an option handler.
    if gcc.nil?
      error "no host compiler package is registered: -H has nothing " \
            "to name a stack with"
      return 1
    end

    if ver.nil? || !gcc.installable_versions.include?(ver)
      names = gcc.installable_versions.map { |v| Coords.stack_name(v) }
      error "Unknown host GCC stack: #{str}"
      error "Available: #{names.join(', ')} " \
            "(the \"gcc-\" prefix is optional)"
      return 1
    end

    pkgmgr.host_stack = ver
    return 0
  end

  def main(argv)

    early_checks
    read_gcc_ver_defaults
    set_gcc_tc_ver
    check_gcc_tc_ver
    create_toolchain_dirs

    # A bad option is a user error, and a user error is a message.
    # `-S notanarch` printed a Ruby backtrace ending in
    # OptionParser::InvalidArgument, which tells the reader nothing
    # they can act on and looks like the tool crashed.
    begin
      options = parse_options(argv)
    rescue OptionParser::ParseError => e
      error e.message
      return 1
    end

    # Before anything reads a coordinate: -H moves the stack that
    # every :stack package installs into, and the compiler they are
    # built against.
    if options[:host_gcc]
      rc = select_host_stack(options[:host_gcc])
      return rc if rc != 0
    end

    # Printed after the options are parsed, so that -q can suppress it.
    # The flag and the QUIET environment variable mean the same thing,
    # but only the variable was ever checked here -- and it is the bash
    # wrapper that sets it. A direct `ruby main.rb -q` stayed noisy, and
    # CMake is documented to call this directly: --check-for-updates
    # says it prints nothing when everything is fine, yet its context
    # dump was arriving folded into CMake's error message.
    if options[:quiet] == 0 && (ENV['QUIET'].blank? || ENV['QUIET'] == '0')
      puts "Context"
      puts "------------------"
      dump_context
      puts
      puts
    end

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

    if options[:clean]
      pkgmgr.refresh()
      n = pkgmgr.clean(options[:dry_run])
      info "#{options[:dry_run] ? "Would remove" : "Removed"}: " \
           "#{n} installation(s)"
      return 0
    end

    if options[:print_layout]
      Layout.print_vars
      return 0
    end

    if options[:check_for_updates]
      pkgmgr.refresh()

      upgrades = pkgmgr.get_upgradable_packages.map(&:name).sort
      stale = pkgmgr.get_stale_packages.map(&:name).sort - upgrades

      return 0 if upgrades.empty? && stale.empty?

      # Two different problems with two different remedies, so they
      # are reported separately: a bumped version needs --upgrade, a
      # package built from sources that have since changed needs a
      # rebuild.
      puts "NEEDS_UPGRADE #{upgrades.join(' ')}" if !upgrades.empty?
      puts "NEEDS_REBUILD #{stale.join(' ')}" if !stale.empty?
      return 2
    end

    pkgmgr.refresh()

    begin
      pkgmgr.validate_deps
    rescue DepResolver::CycleError, DepResolver::MissingDepError => e
      error "Dependency graph error: #{e.message}"
      return 1
    end

    begin
      pkgmgr.validate_versions
    rescue PackageManager::MissingVersionError => e
      error "Version table error: #{e.message}"
      return 1
    end

    if options[:list_stacks]
      pkgmgr.show_stacks
      return 0
    end

    if options[:list]
      pkgmgr.show_status_all(
        options[:group_by],
        options[:compiler].eql?("ALL")
      )
      return 0
    end

    if options[:list_installable]
      # Emit one line per installable package: "<name> <tag>".
      # tag = "default" when the package is itself a default, OR
      # transitively required by a default (both get auto-installed
      # by `build_toolchain` with no arguments). tag = "optional"
      # otherwise. Compilers are included — `-s <full-name>` works
      # on them too, `-S <arch>` is just a shortcut.
      #
      # Order is topological (deps-first) so a consumer installing
      # in listed order keeps each `-s` step small (no hidden dep
      # installs blowing up the per-step timing).
      #
      # Respects -a <arch> if given, so
      # `--list-installable -a riscv64` shows riscv64's set.
      target = options[:arch] ? ALL_ARCHS[options[:arch]] : ARCH
      pkgmgr.with_target_arch(target) do
        installable = pkgmgr.all_packages.reject { |p|
          p.get_installable_list.empty?
        }
        graph = pkgmgr.build_dep_graph
        empty = Set.new
        default_names = pkgmgr.get_default_packages.map(&:name)

        default_order = DepResolver.resolve(default_names, graph, empty)
        default_set = Set.new(default_order)

        full_order = DepResolver.resolve(
          installable.map(&:name), graph, empty
        )

        # A third tag, not a third column: the host world is optional
        # by definition -- none of it is a default -- so this refines
        # "optional" rather than contradicting it. A consumer that
        # wants the packages Tilck is built from can now say so, and
        # the system tests do.
        world = Set.new(pkgmgr.host_world_names)

        full_order.each do |name|
          tag = if world.include?(name)
            "host-world"
          elsif default_set.include?(name)
            "default"
          else
            "optional"
          end

          puts "#{name} #{tag}"
        end
      end
      return 0
    end

    if !options[:deps].blank?
      target = options[:arch] ? ALL_ARCHS[options[:arch]] : ARCH
      pkgmgr.with_target_arch(target) do
        graph = pkgmgr.build_dep_graph
        installed = Set.new
        pkgmgr.all_packages.each { |p|
          installed.add(p.name) if p.installed?(p.default_ver)
        }

        roots = options[:deps].map { |raw|
          name = resolve_pkg_name(raw)
          return 1 if !name
          name
        }

        lines = render_dep_trees(roots, graph,
                                 installed: installed,
                                 show_installed: true,
                                 ascii: options[:ascii])
        puts if !options[:ascii]
        lines.each { |l| puts l }
        puts if !options[:ascii]
      end
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

      if options[:dry_run]
        info "Dry run (-d): nothing upgraded"
        return 0
      end

      for name, ver in plan do
        if !pkgmgr.install(name, ver)
          error "Could not install: #{name}"
          return 1
        end
      end
      return 0
    end

    if options[:config]
      raw, v = options[:config].split(":")
      name = resolve_pkg_name(raw)
      return 1 if !name
      pkg = pkgmgr.get(name)
      if !pkg.configurable?
        error "Package #{pkg.name} does not support reconfiguration"
        return 1
      end
      return pkg.configure(Ver(v)) ? 0 : 1
    end

    if !options[:install].blank?

      # Determine which arch(es) to install for.
      arch_opt = options[:arch]
      if arch_opt == "ALL"
        targets = ALL_ARCHS.values
      elsif arch_opt
        targets = [ALL_ARCHS[arch_opt]]
      else
        targets = [ARCH]
      end

      for target in targets do
        pkgmgr.with_target_arch(target) do

          # When iterating ALL archs, show which one we're on.
          if targets.length > 1
            info "Architecture: #{target.name}"
          end

          # Expand "ALL" entries now, inside the arch scope, so
          # get_installable_list uses the correct arch.
          expanded = expand_install_all(options[:install])

          # Parse "name:ver" pairs, resolving short names.
          requested = expanded.map { |s|
            raw, ver = s.split(":")
            name = resolve_pkg_name(raw)
            return 1 if !name
            [name, Ver(ver)]
          }

          # Validate arch support for each explicitly-requested
          # package (deps are checked later by pkgmgr.install).
          arch_ok = true
          for name, _ver in requested do
            pkg = pkgmgr.get(name)
            next if !pkg || pkg.on_host || pkg.arch_list.nil?
            if !pkg.arch_supported?
              if targets.length > 1
                # -a ALL: skip this arch gracefully.
                info "Skipping #{name}: not supported on " +
                     "#{pkgmgr.target_arch.name}"
                arch_ok = false
                break
              else
                error "Package #{name} is not supported " +
                      "for arch #{pkgmgr.target_arch.name}"
                return 1
              end
            end
          end
          next if !arch_ok

          # Which host stack this invocation builds into: the
          # host_gcc version the request resolves to. `-s
          # host_gcc:13.4.0` therefore builds the 13.4.0 stack — that
          # stack's kernel headers and glibc included — rather than
          # borrowing another compiler's sysroot. HOST_VER_GCC only
          # supplies a version when none was named.
          begin
            stack = pkgmgr.resolved_versions_for(requested)["host_gcc"]
          rescue VersionSolver::ConflictError,
                 VersionSolver::UnstableError => e
            error "Version conflict: #{e.message}"
            return 1
          end

          # Say so when the stack is not the one the context printed.
          # A pin moves it -- asking for QEMU 7 asks for GCC 12 -- and
          # a run whose header says gcc-14.4.0 while it writes into
          # gcc-12.5.0 has told the user the wrong thing about the
          # only coordinate that decides where its work lands.
          if stack && stack != pkgmgr.default_stack_cc_ver
            info "Building into the #{Coords.stack_name(stack)} stack"
          end

          failed = nil

          # Everything from here on runs inside the stack scope: what
          # counts as already-installed, what the plan contains, what
          # the tree shows and what actually gets built all have to
          # agree on which stack this is. Computing any of them outside
          # the scope silently answers about a different one — the tree
          # did exactly that, hiding the dependencies it thought were
          # present because they were, in the OTHER stack.
          plan = nil
          conflict = nil
          done = false

          pkgmgr.with_host_stack(stack) do

            # -f in install mode: force a fresh install by uninstalling
            # each requested package first. Transitive deps are NOT
            # touched — only the explicitly requested packages.
            #
            # INSIDE the stack scope, because force_remove asks the
            # package where the install about to run will write, and
            # outside the scope that is the default stack rather than
            # this one. It removed a tree nobody was rebuilding, the
            # plan then found the real install still present, and the
            # run said both of these in the same breath:
            #
            #   INFO:   Force-removing: host_qemu:6.2.0
            #   INFO: All requested packages are already installed
            #
            # -- a forced rebuild that rebuilt nothing and reported
            # success.
            if options[:force]
              if options[:dry_run]
                info "Force mode (-f): would remove requested packages"
                for name, ver in requested do
                  info "  Would force-remove: #{name}#{ver ? ":#{ver}" : ""}"
                end
              else
                info "Force mode (-f): removing requested packages"
                for name, ver in requested do
                  info "  Force-removing: #{name}#{ver ? ":#{ver}" : ""}"
                  pkgmgr.force_remove(name, ver)
                end
                pkgmgr.refresh()
              end
            end

            begin
              plan = pkgmgr.resolve_install_plan(requested)
            rescue VersionSolver::ConflictError,
                   VersionSolver::UnstableError => e
              conflict = e.message
              next
            end

            if plan.empty?
              # After a forced removal the plan CANNOT be empty: -f
              # just deleted what the install would recreate. An empty
              # one means the removal missed -- wrong stack, wrong
              # filter -- and the run would otherwise report success
              # having rebuilt nothing:
              #
              #   INFO:   Force-removing: host_qemu:6.2.0
              #   INFO: All requested packages are already installed
              #
              # Two separate bugs produced exactly that, and the only
              # evidence either time was three builds finishing in one
              # second.
              if options[:force] && !options[:dry_run]
                error "-f removed nothing that the install would " \
                      "recreate: the removal and the plan disagree " \
                      "about which installation this is"
                failed = requested.map(&:first).join(", ")
                next
              end

              info "All requested packages are already installed"
              done = true
              next
            end

            # Show the install plan as a dependency tree.
            graph = pkgmgr.build_dep_graph
            installed = Set.new
            pkgmgr.all_packages.each { |p|
              installed.add(p.name) if p.installed?(p.default_ver)
            }
            req_names = requested.map(&:first)
            info "Install plan:"
            lines = render_dep_trees(req_names, graph,
                                     installed: installed,
                                     show_installed: false,
                                     ascii: options[:ascii])
            lines.each { |l| puts l }
            puts if !options[:ascii]

            # Everything the plan needs from the host, checked as
            # one batch before the first build starts. A missing Rust
            # toolchain has to stop the run here, not forty minutes
            # in when a configure script finally goes looking.
            if !SystemDeps.check_plan(plan, dry_run: options[:dry_run])
              failed = "unmet system dependencies"
              next
            end

            if options[:dry_run]
              info "Dry run (-d): nothing installed"
              done = true
              next
            end

            for name, ver in plan do
              if !pkgmgr.install(name, ver)
                failed = name
                break
              end
            end
          end

          if conflict
            error "Version conflict: #{conflict}"
            return 1
          end

          next if done

          if failed
            error "Could not install: #{failed}"
            return 1
          end
        end
      end
      return 0
    end

    if !options[:uninstall].blank?
      for entry in options[:uninstall] do
        raw, v = entry.split(":")
        # "ALL" is a literal keyword for uninstall mode; don't resolve it.
        if raw == "ALL"
          name = raw
        elsif pkgmgr.orphan_names.include?(raw)
          # An install on disk that no registered package claims, e.g.
          # left behind by a package rename. `-l` lists it as "found",
          # so it must be removable by that same name.
          name = raw
        else
          name = resolve_pkg_name(raw)
          return 1 if !name
        end
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

    # --contrib: append the contributor-only extras (host_mconf)
    # on top of the default set. Silently skip any that aren't
    # registered — the list is under our control.
    if options[:contrib]
      CONTRIB_EXTRA_PACKAGES.each do |name|
        pkg = pkgmgr.get(name)
        next if pkg.nil?
        next if all.any? { |p| p.name == name }
        all << pkg
      end
    end

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

    # Show the install plan as a dependency tree (same renderer as
    # -s install plans). Roots are the top-level defaults/upgrades
    # that actually have work to do — already-up-to-date packages
    # drop out of the plan and thus also out of the root list, so
    # the tree doesn't get cluttered with bare "no-op" roots.
    graph = pkgmgr.build_dep_graph
    installed = Set.new
    pkgmgr.all_packages.each { |p|
      installed.add(p.name) if p.installed?(p.default_ver)
    }
    plan_set = Set.new(plan.map(&:first))
    root_names = all.map(&:name).select { |n| plan_set.include?(n) }
    info "Install plan:"
    lines = render_dep_trees(root_names, graph,
                             installed: installed,
                             show_installed: false,
                             ascii: options[:ascii])
    lines.each { |l| puts l }
    puts if !options[:ascii]

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
