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
require_relative 'gperf'
require_relative 'meson'
require_relative 'pixman'
require_relative 'libslirp'
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
require_relative 'tilck_stack'

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
  def read_gcc_ver_defaults
    conf = MAIN_DIR / "other" / "gcc_tc_conf"
    for name, arch in ALL_ARCHS do
      arch.min_gcc_ver = Ver(File.read(conf / name / "min_ver"))
      arch.default_gcc_ver = Ver(File.read(conf / name / "default_ver"))
      arch.gcc_ver = arch.default_gcc_ver
    end
  end

  # A name as typed to the registered one (Planner.resolve_name), for
  # the modes that print as they go: the full name, or nil once the
  # refusal is printed.
  def resolve_pkg_name(input)
    r = Planner.resolve_name(pkgmgr, input)
    if r.is_a?(Refusal)
      r.message.each_line { |l| error l.chomp }
      return nil
    end
    full, note = r
    info note if note
    return full
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
  # A subtree is drawn once. Dependency graphs are diamonds all the
  # way down -- gmp under mpfr, under mpc, under isl, under gcc, under
  # everything built with gcc -- and a tree that redraws a shared
  # subtree at every mention grows exponentially in its depth: the
  # plan for host_qemu ran to twenty thousand lines for a closure of
  # fifty packages. So a package is expanded where the walk first
  # reaches it, and every later mention names it with "(+ deps)",
  # which points back up at the one drawing. That set is shared by
  # every root of one render, for the same reason.
  #
  # Returns an Array of ready-to-puts strings.
  def render_dep_trees(roots, graph, installed: Set.new,
                       show_installed: false, ascii: false)
    lines = []
    expanded = Set.new
    roots.each_with_index do |name, ri|
      lines << "" if ri > 0 && !ascii
      if ascii
        dep_tree_ascii(name, graph, installed, show_installed,
                       lines, "", expanded)
      else
        dep_tree_root(name, graph, installed, show_installed,
                      lines, expanded)
      end
    end
    lines
  end

  # Every package a tree shows, each once, dependencies before the
  # packages that need them: the closure, flat. What the reader wants
  # after the picture -- how many, and which -- and what the picture
  # cannot show without repeating itself.
  def dep_tree_closure(roots, graph, installed: Set.new,
                       show_installed: false)
    seen = Set.new
    out = []
    add = ->(name) {
      next if seen.include?(name)
      seen << name
      dep_tree_deps(name, graph, installed, show_installed).each(&add)
      out << name
    }
    roots.each(&add)
    out
  end

  # `names` as a comma-separated paragraph that fits in 80 columns,
  # indented like the tree. Installed ones are dimmed in --deps mode,
  # as they are in the tree above them.
  def render_name_list(names, installed: Set.new, show_installed: false)
    rows = [[]]
    names.each do |n|
      row = rows.last
      width = LEAD.length + (row + [n]).join(", ").length + 1  # +1: the ","
      rows << (row = []) if !row.empty? && width > 80
      row << n
    end

    rows.map.with_index { |row, i|
      text = row.map { |n| dep_tree_fmt(n, installed, show_installed) }
                .join(", ")
      LEAD + text + (i == rows.length - 1 ? "" : ",")
    }
  end

  # The flat list under a tree: a heading, the names, and the blank
  # line the fancy mode puts after every block.
  def show_name_list(heading, names, ascii, installed: Set.new,
                     show_installed: false)
    info heading
    render_name_list(names, installed: installed,
                            show_installed: show_installed)
      .each { |l| puts l }
    puts if !ascii
  end

  # --- ASCII (machine-friendly) mode ---

  def dep_tree_ascii(name, graph, installed, show_installed,
                     lines, indent, expanded)
    deps, elided = dep_tree_visit(name, graph, installed, show_installed,
                                  expanded)
    lines << "#{indent}#{name}#{elided ? ELIDED : ''}"
    deps.each do |dep|
      dep_tree_ascii(dep, graph, installed, show_installed,
                     lines, indent + "  ", expanded)
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
  ELIDED = " (+ deps)"

  def dep_tree_root(name, graph, installed, show_installed, lines,
                    expanded)
    deps, elided = dep_tree_visit(name, graph, installed, show_installed,
                                  expanded)

    # Root with no visible subtree uses a bare "─ " bullet rather
    # than the "┌ " corner — there is no trunk to open.
    corner = deps.empty? ? "─" : "┌"
    lines << "#{LEAD}#{corner} " +
             dep_tree_fmt(name, installed, show_installed, elided: elided)

    if deps.empty?
      if show_installed && !elided
        lines << "#{LEAD}(no dependencies)"
      end
      return
    end

    spacer = "#{LEAD}│"
    lines << spacer
    deps.each_with_index do |dep, i|
      last = (i == deps.length - 1)
      dep_tree_child(dep, graph, LEAD, last, lines, installed,
                     show_installed, expanded)
      lines << spacer if !last
    end
  end

  def dep_tree_child(name, graph, prefix, is_last, lines, installed,
                     show_installed, expanded)
    deps, elided = dep_tree_visit(name, graph, installed, show_installed,
                                  expanded)
    conn = is_last ? "└── " : "├── "
    lines << "#{prefix}#{conn}" +
             dep_tree_fmt(name, installed, show_installed, elided: elided)
    return if deps.empty?

    child_prefix = prefix + (is_last ? "    " : "│   ")
    spacer = "#{child_prefix}│"
    lines << spacer

    deps.each_with_index do |dep, i|
      last = (i == deps.length - 1)
      dep_tree_child(dep, graph, child_prefix, last, lines, installed,
                     show_installed, expanded)
      lines << spacer if !last
    end
  end

  # --- Shared helpers ---

  # What one mention of `name` shows: its visible deps, to be drawn
  # under it, or none and the "(+ deps)" mark because they were drawn
  # under an earlier mention. A package with nothing visible under it
  # is never marked -- nothing is being left out. Marking happens
  # before the descent, so a cycle, should one ever get past the
  # resolver, ends at its first repeat instead of never.
  def dep_tree_visit(name, graph, installed, show_installed, expanded)
    deps = dep_tree_deps(name, graph, installed, show_installed)
    return [deps, false] if deps.empty?
    return [[], true] if expanded.include?(name)
    expanded << name
    return [deps, false]
  end

  def dep_tree_deps(name, graph, installed, show_installed)
    deps = graph[name] || []
    show_installed ? deps : deps.reject { |d| installed.include?(d) }
  end

  def dep_tree_fmt(name, installed, show_installed, elided: false)
    text = if show_installed && installed.include?(name)
      "#{Term::DIM}#{name}#{Term::RESET}"
    else
      name
    end
    text += "#{Term::DIM}#{ELIDED}#{Term::RESET}" if elided
    text
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
    # stack at its default board. None for an arch with no board --
    # aarch64, a cross compiler only so far: a board is the <env> of
    # a target's coordinates, and an arch without one has no place
    # for a package. Its directory used to be created as
    # tilck-aarch64/any/gcc-13.3.0 on every run, giving `any` a
    # second meaning beside "no environment requirement".
    for name, arch in ALL_ARCHS do
      arch.target_dir = arch.default_board.nil? ? nil :
        Coords.target(arch, arch.default_board, arch.gcc_ver).pkgs_dir
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

  def dump_context(scope)

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
      ARCH
      BOARD
      DEFAULT_BOARD
    ]

    list.each { |x| puts "#{x} = #{de.call(x)}" }

    # The host, as the scope carries it (Host.env at the boundary).
    puts "HOST_ARCH = #{scope.host.arch.name}"
    puts "HOST_OS = #{scope.host.os}"
    puts "HOST_DISTRO = #{scope.host.distro}"
    puts "HOST_CC = #{scope.host.cc}"

    # Not a constant: -H moves it, and a run that built into another
    # stack should say so where every other coordinate is printed.
    puts "HOST_STACK = #{Coords.stack_name(scope.stack)}"

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
    # Every board of every arch has a BSP directory under other/bsp,
    # even x86's "pc", whose only content is a note that there is no
    # board data for a PC. So a board the arch does not have is caught
    # here, for every arch, the way CMake catches it: by the directory
    # that is not there.
    if BOARD && !board_bsp.exist?
      error "BOARD_BSP: #{board_bsp} not found!"
      exit 1
    end
  end

  def create_toolchain_dirs
    for name, arch in ALL_ARCHS do
      mkdir_p(arch.target_dir) if arch.target_dir
    end
  end

  # The defaults every parse starts from.
  def option_defaults
    return {
      help: false,
      skip_install_pkgs: false,
      just_context: false,
      dry_run: false,
      board: nil,
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
      rebuild: false,
      config: nil,
      install: [],
      mark_manual: [],
      mark_auto: [],
      autoremove: false,
      install_compiler: [],
      uninstall: [],
      uninstall_compiler: [],
      arch: nil,
      compiler: nil,
      group_by: nil,
      quiet: 0,
    }
  end

  # The options that are modes: at most one per command line.
  MODE_OPTS = [
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
    :rebuild,
    :config,
    :install,
    :install_compiler,
    :uninstall,
    :uninstall_compiler,
    :mark_manual,
    :mark_auto,
    :autoremove,
  ].freeze

  # The parser is a table of switches, built once when the module
  # loads. What changes from one parse to the next is where the
  # handlers write and which argv the multi-word ones read, so those
  # two are the parse's state (@opts, @argv), not the parser's. One
  # process, one main: the same cost as before. One process, ten
  # thousand mains -- the test suite, the exhaustive lane, every
  # mutant judged by them -- and building ~45 switches per main was
  # a fifth of a case.
  def build_parser

    is_option = ->(line) { line.lstrip.start_with?("-") }
    # Coloured when the stream WRITTEN is a terminal -- $stdout, which
    # a test or a pipe replaces -- not when the process's own is:
    # measured on a redirected $stdout, the escape codes counted as
    # columns and the help was eleven wider than the terminal.
    highlight = ->(line) {
      return line if not $stdout.tty?
      line.sub!("[MODE]", "[#{Term.makeGreen("MODE")}]")
      line.sub!("[FLAG]", "[#{Term.makeYellow("FLAG")}]")
      line.sub!("[OPTION]", "[#{Term.makeYellow("OPTION")}]")
      line.sub!("ALL", Term.makeRed("ALL"))
      line
    }
    # The summary as OptionParser writes it -- the switch in a column,
    # the description beside it, a line per string given to on() --
    # re-flowed so that every line fits the terminal: 80 columns, or
    # the terminal's own width up to 120 (Term.columns). The strings
    # in the source are wrapped for the source's sake, not the
    # screen's; the column stays where OptionParser put it, and the
    # words of each description are laid out again to the right of it.
    reformat_summary = ->(parser, summary) {
      col = parser.summary_indent.length + parser.summary_width + 1
      room = [Term.columns - col, 20].max
      blocks = []
      curr = nil
      summary.each { |line|
        line = line.chomp
        if is_option.(line)
          blocks << curr if curr
          head, desc = line[0, col], line[col..].to_s
          # A switch too long for its column: OptionParser puts the
          # description on the next line, and so do we.
          if head.length == col && head.end_with?(" ")
            curr = { head: head, words: desc.split }
          else
            curr = { head: line, words: [] }
          end
        elsif curr && line.start_with?(" " * col)
          curr[:words] += line.split
        else
          blocks << curr if curr
          curr = nil
          blocks << { head: line, words: [] }
        end
      }
      blocks << curr if curr

      out = []
      blocks.each { |b|
        out << "" if !out.empty?      # a blank line between options
        lines = []
        row = +""
        b[:words].each { |w|
          if !row.empty? && row.length + 1 + w.length > room
            lines << row
            row = +""
          end
          row << (row.empty? ? w : " #{w}")
        }
        lines << row if !row.empty?
        if lines.empty?
          out << b[:head]
        else
          first = b[:head]
          first += "\n" + " " * col if first.length != col
          out << first + lines.first
          lines.drop(1).each { |l| out << " " * col + l }
        end
      }
      out.map { |l| highlight.call(l) }.join("\n") + "\n"
    }



    get_multiple_args = ->(first, sym) {
      list = [first]
      while @argv.first && @argv.first !~ /\A-/
        list << @argv.shift
      end
      @opts[sym] += list
    }

    p = OptionParser.new('./scripts/build_toolchain [-n] [OPTIONS]')

    p.on('-h', '--help', 'Show this help message [MODE]') {
      @opts[:help] = true
      puts p.banner
      puts
      puts reformat_summary.call(p, p.summarize())
    }

    p.on('-l', '--list',
         'List all packages status. The host stacks are a line each,',
         'with how many packages every one holds: the listing shows',
         'one stack, the current one (-H picks another). [MODE]') {
      @opts[:list] = true
    }

    p.on('-L', '--list-stacks',
         'List the host stacks: which compilers a world has been',
         'built with, and how many packages are in each [MODE]') {
      @opts[:list_stacks] = true
    }

    p.on('-H', '--host-gcc STACK',
         'Build for, and look at, the stack of the given host GCC',
         'instead of the one HOST_VER_GCC names. Applies to every',
         'mode: -s builds into that stack, -l and -L report on it.',
         'Written either way: "gcc-14.4.0", as -L prints it, or the',
         'bare "14.4.0". The stack does not have to exist yet --',
         'asking for it is what builds it. [OPTION]') { |v|
      @opts[:host_gcc] = v
    }

    p.on('--list-installable',
         'Print package names installable via -s for the current ARCH,',
         'one per line, no decoration. Machine-readable output for',
         'tooling (e.g. system tests that need to filter per-arch',
         'supported packages) [MODE]') {
      @opts[:list_installable] = true
    }

    # No description line may begin with a dash: OptionParser reads
    # such a string as one more switch of the option, and "-a <arch>
    # for cross-arch queries" gave -D an argument spec of prose.
    p.on('-D', '--deps PKG',
         'Show the dependency tree for the given package(s).',
         'Already-installed deps are shown in gray. Respects the',
         'arch given with -a, for cross-arch queries. [MODE]') do |first|
      get_multiple_args.call(first, :deps)
    end

    p.on('--ascii',
         'Use plain-text indented output for dependency trees.',
         'Machine-friendly alternative to the fancy box-drawing',
         'format. Applies to --deps, -s install plans, and the',
         'default-install plan. [FLAG]') {
      @opts[:ascii] = true
    }

    p.on('-j', '--just-context', 'Just show the context and quit [MODE]') {
      @opts[:just_context] = true
    }

    p.on('-t', '--self-test', 'Run internal unit tests [MODE]') {
      @opts[:self_test] = true
    }

    p.on('--coverage',
         'Collect code coverage data + HTML report (use with -t) [FLAG]') {
      @opts[:coverage] = true
    }

    p.on('--system-tests',
         'After unit tests: install all pkgs and build Tilck for the',
         'archs -a names and the boards -b names [FLAG]') {
      @opts[:system_tests] = true
    }

    p.on('--all-build-types',
         'With --system-tests: build all generator configs too [FLAG]') {
      @opts[:all_build_types] = true
    }

    p.on('--run-also-tilck-tests',
         'With --system-tests: run gtests + system tests (i386/riscv64) [FLAG]') {
      @opts[:run_tilck_tests] = true
    }

    p.on('-F', '--filter REGEX',
         'Run only tests matching REGEX (use with -t) [OPTION]') {
      |pat| (@opts[:test_args] ||= []) << "--filter" << pat
    }

    p.on('-V', '--verbose-tests',
         'Show stdout/stderr even for passing tests (use with -t) [FLAG]') {
      (@opts[:test_args] ||= []) << "--verbose-tests"
    }

    p.on('--exhaustive',
         'After unit tests: every small world x every command line, ' \
         'against the model (use with -t) [FLAG]') {
      (@opts[:test_args] ||= []) << "--exhaustive"
    }

    p.on('--mutation',
         'After unit tests: every mutant of the logic core must die ' \
         '(use with -t) [FLAG]') {
      (@opts[:test_args] ||= []) << "--mutation"
    }

    p.on('--seed N', 'Seed of the sampled exhaustive lane [OPTION]') {
      |n| (@opts[:test_args] ||= []) << "--seed" << n
    }

    p.on('--case ID', 'Replay one exhaustive case by id [OPTION]') {
      |id| (@opts[:test_args] ||= []) << "--case" << id
    }

    p.on('--jobs N', 'Processes for --exhaustive [OPTION]') {
      |n| (@opts[:test_args] ||= []) << "--jobs" << n
    }

    p.on('--test-packages-filter REGEX',
         'With --system-tests: install only optional packages matching REGEX') {
      |pat| (@opts[:test_args] ||= []) << "--test-packages-filter" << pat
    }

    p.on(
      '-C', '--config PKG[:VER]',
      'Reconfigure the given version (optional) of a package',
      'interactively (e.g. make menuconfig) [MODE]'
    ) { |pkg| @opts[:config] = pkg }

    p.on(
      '--upgrade',
      'Upgrade installed packages whose version was bumped in',
      'pkg_versions. Does not install new packages. [MODE]'
    ) { @opts[:upgrade] = true }

    p.on(
      '--rebuild',
      'Rebuild every install made from sources that have since',
      'changed (a patch, a flag, the recipe), each where it is and at',
      'its own version. What --check-for-updates lists as',
      'NEEDS_REBUILD; a bumped version is --upgrade\'s. [MODE]'
    ) { @opts[:rebuild] = true }

    p.on(
      '--check-for-updates',
      'Check if any installed packages need upgrading. Prints nothing',
      'and exits 0 if up to date, or prints the list and exits 2 if',
      'upgrades are needed. Lightweight: meant to be called directly',
      'by CMake without the bash wrapper. [MODE]'
    ) { @opts[:check_for_updates] = true }

    p.on(
      '--clean',
      'Uninstall everything, keeping the prebuilt cross-compilers, the',
      'bootstrap Ruby and the download cache. What is left is what a',
      'fresh checkout would download anyway, so the rebuild after it',
      'is a real one. Combine with -d to see what would go. [MODE]'
    ) { @opts[:clean] = true }

    p.on(
      '--print-layout',
      'Print the installed-package directories as KEY=value lines, so',
      'that the build system does not have to reconstruct them from',
      'the layout schema. Reads ARCH, BOARD and GCC_TC_VER from the',
      'environment like every other mode. Lightweight: meant to be',
      'called directly by CMake without the bash wrapper. [MODE]'
    ) { @opts[:print_layout] = true }

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
      '--mark-manual PKG[:VER]',
      'Mark the given installation as asked for by name, so that it',
      'survives --autoremove. Takes the modifiers -u takes: -a, -c, a',
      'version. [MODE]'
    ) do |first|
      get_multiple_args.call(first, :mark_manual)
    end

    p.on(
      '--mark-auto PKG[:VER]',
      'Mark the given installation as installed only as a dependency,',
      'so that --autoremove may take it once nothing needs it. Takes',
      'the modifiers -u takes: -a, -c, a version. [MODE]'
    ) do |first|
      get_multiple_args.call(first, :mark_auto)
    end

    p.on(
      '--autoremove',
      'Remove every installation that was pulled in as a dependency',
      'and that nothing still installed needs, like apt. Combine it',
      'with -d to see what would go. [MODE]'
    ) { @opts[:autoremove] = true }

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
      @opts[:dry_run] = true
    }

    p.on('-g', '--group-by WHAT', ['ver', 'arch'],
         'Group packages by "ver" or "arch" [OPTION]') { |what|
      @opts[:group_by] = what
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

      @opts[:compiler] = value
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

      @opts[:arch] = value
    end

    p.on(
      '-b', '--board BOARD',
      'Board of the target arch for the current operation, the way -a',
      'names the arch: a scope for the modes that build (-s and the',
      'default install), a filter for -u and the marks. A board belongs',
      'to one arch, so a name is refused with -a ALL. The special value',
      'ALL means every board of the arch. [OPTION]'
    ) do |value|

      known = ALL_ARCHS.values.any? { |a| a.all_boards.include?(value) }
      if value != "ALL" && !known
        raise OptionParser::InvalidArgument, "Unknown board: #{value}"
      end

      @opts[:board] = value
    end

    p.on(
      '-q', 'Be quiet: skip the bootstrap logging [FLAG]'
    ) { @opts[:quiet] = 1 }

    p.on(
      '-f', '--force',
      'Force. Meaning depends on the MODE. In uninstall mode, this includes',
      'the cross-compilers, when the package name is ALL. In install mode',
      '(-s), this forces an uninstall+install cycle for each requested',
      'package even if already installed. [FLAG]'
    ) { @opts[:force] = true }

    p.on(
      '-n', '--skip-install-pkgs',
      'Do not check/install system dependencies. This flag is useful when the',
      'user run at least *one* time this script without this flag so that the',
      'necessary packages have been installed and the system configuration nor',
      'the dependencies in the source have changed since then. Using this flag',
      'improves the speed, but it is generally discouraged, unless this script',
      'is run on a *unsupported* Linux distribution or the user is experienced',
      'with Tilck\'s package manager and prepared to handle a failure. [FLAG]'
    ) { @opts[:skip_install_pkgs] = true }

    p.on(
      '--contrib',
      'When combined with the default install (no mode flag), also',
      'install packages useful for contributors: host_mconf',
      '(plus its host_ncurses dep). Intended to be run once, like:',
      './scripts/build_toolchain --contrib. Packages listed in',
      'Planner::CONTRIB_EXTRAS are appended to the normal default',
      'set before the plan is resolved. [FLAG]'
    ) { @opts[:contrib] = true }

    return p
  end

  PARSER = build_parser

  def parse_options(argv = ARGV.dup)
    @opts = option_defaults
    @argv = argv
    PARSER.parse!(argv)
    opts = @opts
    mode_opts = MODE_OPTS
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
          next ALL_ARCHS.values.map { |a| "#{a.cross_cc_pkg}:#{ver}" }
        end
        arch_obj = ALL_ARCHS[arch]
        if !arch_obj
          raise OptionParser::InvalidArgument, "Unknown architecture: #{arch}"
        end
        ["#{arch_obj.cross_cc_pkg}:#{ver}"]
      }
    end

    # NOTE: -s ALL expansion moved to main(), inside the
    # with_target_arch scope, so it respects -a <arch>.

    return opts
  end

  # The scope a query (--list-installable, --deps) is asked at: the
  # invocation's, at the arch -a names and the board -b names. ALL on
  # either is the scope's own -- a query is asked at one place. A
  # board the arch does not have is refused here, as the planner
  # refuses it for the modes it plans (Planner.board_refusal); nil,
  # having said why.
  def requested_scope(scope, o)
    arch = ALL_ARCHS[o[:arch]] || scope.arch
    board = o[:board] == "ALL" ? nil : o[:board]
    if board && !arch.all_boards.include?(board)

      error "Unknown board #{board} for #{arch.name}"
      return nil
    end
    return scope.with(arch: arch, board: board)
  end

  # The parsed options as a Request (request.rb): the one value the
  # planner is handed. The modes the planner has no say in (--deps,
  # -L, the help) are :other.
  def request_of(o)
    words = ->(list) { list.map { |w| Request.target(w) } }
    mode, targets =
      if !o[:install].blank?      then [:install, words.call(o[:install])]
      elsif !o[:uninstall].blank? then [:uninstall, words.call(o[:uninstall])]
      elsif o[:config]            then [:configure, words.call([o[:config]])]
      elsif !o[:mark_manual].blank?
        [:mark_manual, words.call(o[:mark_manual])]
      elsif !o[:mark_auto].blank? then [:mark_auto, words.call(o[:mark_auto])]
      elsif o[:upgrade]           then [:upgrade, []]
      elsif o[:rebuild]           then [:rebuild, []]
      elsif o[:autoremove]        then [:autoremove, []]
      elsif o[:clean]             then [:clean, []]
      elsif o[:list]              then [:list, []]
      elsif o[:check_for_updates] then [:check_updates, []]
      elsif o[:list_installable]  then [:installable, []]
      elsif o[:print_layout]      then [:layout, []]
      elsif o[:just_context]      then [:context, []]
      elsif o[:help] || o[:self_test] || o[:list_stacks] || !o[:deps].blank?
        [:other, []]
      else [:default, []]
      end

    arch = if o[:arch] == "ALL" then :all
           elsif o[:arch] then ALL_ARCHS.fetch(o[:arch])
           end
    board = o[:board] == "ALL" ? :all : o[:board]
    cc = case o[:compiler]
         when nil, "syscc" then o[:compiler]
         when "ALL"        then :all
         else Ver(o[:compiler])
         end
    stack = o[:host_gcc] ? Coords.parse_stack_ver(o[:host_gcc]) : nil

    return Request.make(mode, targets: targets, force: o[:force],
                        dry: o[:dry_run], arch: arch, board: board, cc: cc,
                        stack: stack, contrib: !!o[:contrib])
  end

  # -H names a stack, either as it is spelled everywhere else --
  # "gcc-14.4.0", which is what -L prints and what the path holds --
  # or as the bare version that names it just as unambiguously.
  #
  # It does not have to be built yet: asking for a stack is how it
  # gets built. It does have to be one the compiler package knows how
  # to build, or the whole run would go to coordinates nothing can
  # ever fill. The version, or nil once it has said why.
  def select_host_stack(str)

    gcc = pkgmgr.stack_compiler
    id = Coords.parse_stack(str)

    # Nil only when no compiler package is registered at all, which
    # is a broken registry rather than a user error -- but saying so
    # beats a NoMethodError from inside an option handler.
    if gcc.nil?
      error "no host compiler package is registered: -H has nothing " \
            "to name a stack with"
      return nil
    end

    # A variant or foreign stack is a legal coordinate the tool cannot
    # build into yet: said as that, not as an unknown name.
    if id && !id.plain?
      error "Cannot build into #{id}: only plain gcc stacks can be " \
            "selected, so far"
      return nil
    end

    if id.nil? || !gcc.installable_versions.include?(id.ver)
      names = gcc.installable_versions.map { |v| Coords.stack_name(v) }
      error "Unknown host GCC stack: #{str}"
      error "Available: #{names.join(', ')} " \
            "(the \"gcc-\" prefix is optional)"
      return nil
    end

    return id.ver
  end

  # --- what a plan looks like on the terminal -------------------------------

  def show_removals(plan, dry)
    info "Force mode (-f): #{dry ? 'would remove' : 'removing'} requested " \
         "packages"
    for r in plan.removes do
      i = r.install
      info "  #{dry ? 'Would force-remove' : 'Force-removing'}: " \
           "#{i.pkgname}:#{i.ver}"
    end
  end

  def show_marks(plan, dry)
    for m in plan.marks do
      info "#{dry ? 'Would set' : 'Set'} #{m.install.pkgname}:" \
           "#{m.install.ver} to " \
           "#{m.manual ? 'manually' : 'automatically'} installed"
    end
  end

  # The install plan as a dependency tree, then as the list in build
  # order. Installed, for the tree, is what is at its bound version
  # at the plan's scope.
  def show_plan(plan, roots, ascii)
    graph = Planner.graph(pkgmgr, plan.scope)
    installed = Set.new
    pkgmgr.all_packages.each { |p|
      b = p.at(plan.scope, world: pkgmgr.world)
      installed.add(p.name) if b.installed?(b.default_ver)
    }
    info "Install plan:"
    lines = render_dep_trees(roots, graph, installed: installed,
                             show_installed: false, ascii: ascii)
    lines.each { |l| puts l }
    puts if !ascii
    show_name_list("#{plan.builds.length} package(s) to install, in " \
                   "this order:", plan.builds.map(&:name), ascii)
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
    # The invocation's scope: the shell's ARCH and BOARD, and the stack
    # -H named, else the one the configuration does. Built once, here,
    # and handed to everything that asks a scoped question.
    stack = pkgmgr.default_stack_cc_ver
    if options[:host_gcc]
      stack = select_host_stack(options[:host_gcc])
      return 1 if stack.nil?
    end
    scope = Scope.env(stack: stack)

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
      dump_context(scope)
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
      args << "--test-board" << options[:board] if options[:board]
      args += options[:test_args] if options[:test_args]
      # exec into a fresh Ruby process so Coverage.start runs before
      # any pkgmgr modules are loaded (coverage only tracks files
      # loaded after start).
      exec(RbConfig.ruby, *args)
    end

    req = request_of(options)

    if req.mode == :clean
      pkgmgr.refresh()
      return run_outcome(Planner.step(pkgmgr, pkgmgr.world, req, scope),
                         req, scope)
    end

    if options[:print_layout]
      Layout.print_vars(scope)
      return 0
    end

    if req.mode == :check_updates
      judged = pkgmgr.world.judged(pkgmgr, scope)
      out = Planner.step(pkgmgr, judged, req, scope)
      out.notes.each { |l| puts l }
      return out.rc
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
      pkgmgr.show_stacks(scope: scope)
      puts
      return 0
    end

    if options[:list]
      pkgmgr.show_status_all(
        options[:group_by],
        options[:compiler].eql?("ALL"),
        scope: scope
      )
      return 0
    end

    if options[:list_installable]
      # One line per installable package: "<name> <tag>", in dependency
      # order so a consumer installing in listed order keeps each -s
      # step small. Respects -a <arch>: `--list-installable -a riscv64`
      # shows riscv64's set. See Planner.installable for the tags.
      sc = requested_scope(scope, options)
      return 1 if sc.nil?
      for name, tag in Planner.installable(pkgmgr, sc) do
        puts "#{name} #{tag}"
      end
      return 0
    end

    if !options[:deps].blank?
      sc = requested_scope(scope, options)
      return 1 if sc.nil?
      begin
        graph = Planner.graph(pkgmgr, sc)
        installed = Set.new
        pkgmgr.all_packages.each { |p|
          b = p.at(sc, world: pkgmgr.world)
          installed.add(p.name) if b.installed?(b.default_ver)
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

        closure = dep_tree_closure(roots, graph, installed: installed,
                                   show_installed: true)
        show_name_list("#{closure.length} package(s) in all, " \
                       "dependencies first:", closure, options[:ascii],
                       installed: installed, show_installed: true)
      end
      return 0
    end

    # Everything that changes the tree: one Request, one step, the
    # acts it comes to printed and run in order. --rebuild reads the
    # records, so its world is judged first.
    world = pkgmgr.world
    world = world.judged(pkgmgr, scope) if req.mode == :rebuild
    return run_outcome(Planner.step(pkgmgr, world, req, scope), req, scope)
  end # method main()

  # --- running what a step came to ------------------------------------------

  # Print and run an Outcome: its notes, then each act -- an arch
  # label, the act's notes, its plan -- and last the refusal, if the
  # step ended in one after the acts before it. An act whose run
  # fails ends the run there, as it always did.
  def run_outcome(out, req, scope)
    out.notes.each { |n| info n }
    for act in out.acts do
      if act.arch
        info "Architecture: #{act.arch.name}" +
             (act.board ? ", board: #{act.board}" : "")
      end

      act.notes.each { |n| info n }
      rc = run_act(act, req, scope)
      return rc if rc != 0
    end
    out.message&.each_line { |l| error l.chomp }
    return out.rc
  end

  # One act, the way its mode says it: what to print around the plan,
  # and whether to hand it to the executor.
  def run_act(act, req, scope)
    plan = act.plan
    dry = req.dry

    case req.mode
    when :configure
      return 0 if dry
      name, v = act.roots
      pkg = pkgmgr.get(name)
      return pkg.at(scope, world: pkgmgr.world).configure(v) ? 0 : 1

    when :install
      return 0 if plan.nil?          # an arch skipped, and said
      plan.notes.each { |n| info n }

      # Say so when the stack is not the one the context printed. A
      # pin moves it -- asking for QEMU 7 asks for GCC 12 -- and a
      # run whose header says gcc-14.4.0 while it writes into
      # gcc-12.5.0 has told the user the wrong thing about the only
      # coordinate that decides where its work lands.
      if plan.scope.stack != scope.stack
        info "Building into the #{Coords.stack_name(plan.scope.stack)} stack"
      end

      show_removals(plan, dry) if req.force
      show_marks(plan, dry)

      if plan.builds.empty?
        Executor.run(pkgmgr, plan) if !dry
        info "All requested packages are already installed"
        return 0
      end

      show_plan(plan, act.roots, @opts[:ascii])

      # Everything the plan needs from the host, checked as one batch
      # before the first build starts. A missing Rust toolchain has
      # to stop the run here, not forty minutes in when a configure
      # script finally goes looking.
      pairs = plan.builds.map { |b| [b.name, b.ver] }
      if !SystemDeps.check_plan(pairs, dry_run: dry)
        error "Could not install: unmet system dependencies"
        return 1
      end

      if dry
        info "Dry run (-d): nothing installed"
        return 0
      end

      failed = Executor.run(pkgmgr, plan)
      if failed
        error "Could not install: #{failed}"
        return 1
      end
      return 0

    when :default
      plan.notes.each { |n| info n }
      show_marks(plan, dry)

      if plan.builds.empty?
        Executor.run(pkgmgr, plan) if !dry
        info "All default packages are installed and up to date"
        return 0
      end

      upgrade_names = act.upgrades & plan.builds.map(&:name)
      if !upgrade_names.empty?
        info "Packages to upgrade: #{upgrade_names.join(', ')}"
      end

      # Roots are the top-level defaults/upgrades that actually have
      # work to do -- already-up-to-date packages drop out of the plan
      # and thus also out of the root list, so the tree is not
      # cluttered with bare no-op roots.
      plan_set = Set.new(plan.builds.map(&:name))
      show_plan(plan, act.roots.select { |n| plan_set.include?(n) },
                @opts[:ascii])

      # The one mode that never checked -d. Found by the exhaustive
      # lane: `-d` with no mode installed the defaults.
      if dry
        info "Dry run (-d): nothing installed"
        return 0
      end

      failed = Executor.run(pkgmgr, plan)
      if failed
        error "Could not install: #{failed}"
        return 1
      end
      return 0

    when :upgrade
      plan.notes.each { |n| info n }
      return 0 if plan.builds.empty?
      info "Packages to upgrade: #{plan.builds.map(&:name).join(', ')}"
      if dry
        info "Dry run (-d): nothing upgraded"
        return 0
      end
      failed = Executor.run(pkgmgr, plan)
      if failed
        error "Could not install: #{failed}"
        return 1
      end
      return 0

    when :rebuild
      plan.notes.each { |n| info n }
      return 0 if plan.empty?
      if dry
        info "Dry run (-d): nothing rebuilt"
        return 0
      end
      begin
        failed = Executor.run(pkgmgr, plan)
      rescue Executor::Failed => e
        # A recipe that raises mid-build -- a dependency it cannot
        # find -- has said what is wrong; the run ends on that, not on
        # a traceback, and the old tree is back where it was.
        error e.message
        failed = e.name
      end
      if failed
        error "Could not rebuild: #{failed}"
        return 1
      end
      return 0

    when :uninstall, :clean
      return 0 if plan.nil?
      plan.notes.each { |n| warning n }
      pkgmgr.say_removals(plan, dry)
      Executor.run(pkgmgr, plan) if !dry
      if req.mode == :clean
        info "#{dry ? "Would remove" : "Removed"}: " \
             "#{plan.removes.length} installation(s)"
      end
      return 0

    when :mark_manual, :mark_auto
      return 0 if plan.nil?
      plan.notes.each { |n| warning n }
      p = "[DRY RUN] " if dry
      how = req.mode == :mark_manual ? "manually installed"
                                     : "automatically installed"
      for m in plan.marks do
        i = m.install
        puts "#{p}Mark #{i.pkgname}:#{i.ver} at #{i.coords} as #{how}"
      end
      Executor.run(pkgmgr, plan) if !dry
      return 0

    when :autoremove
      plan.notes.each { |n| info n }
      pkgmgr.say_removals(plan, dry)
      Executor.run(pkgmgr, plan) if !dry
      n = plan.removes.length
      info "#{dry ? "Would remove" : "Removed"}: #{n} installation(s)" if n > 0
      return 0

    else
      raise ArgumentError, "no act for #{req.mode.inspect}"
    end
  end
end # module Main

if __FILE__ == $0
  exit Main::main(ARGV)
end
