# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'package'
require_relative 'dep_resolver'
require_relative 'version_solver'
require_relative 'sysroot'
require_relative 'install_selector'
require_relative 'planner'
require_relative 'executor'
require_relative 'portability'

require 'singleton'
require 'set'

class PackageManager

  class MissingVersionError < StandardError; end

  include Singleton
  attr_reader :packages

  def initialize
    @packages = {}
    @config_versions = read_config_versions("pkg_versions", "VER_")
    @host_config_versions =
      read_config_versions("host_pkg_versions", "HOST_VER_")
    @world = nil          # what is installed, scanned once per change
    @host_world = nil     # memoized host_world_names, per registry
    @default_stack = nil  # what HOST_VER_GCC would say, when a test
                          # says otherwise (default_stack=)
  end

  # THE ENVIRONMENT'S SCOPE (scope.rb): what an invocation resolves to
  # before any flag moves it -- the shell's ARCH and BOARD, and the
  # stack the configuration names. A function of constants and the
  # version table, computed on every read because the tests swap the
  # constants. Main builds its scope from this and its options; the
  # methods below take a scope and default to this one, for the tests
  # that call them directly. Nothing here remembers a scope: a
  # question about one installation is answered from the scope in
  # hand, or not at all (Package::Unbound).
  def env_scope = Scope.env(stack: default_stack_cc_ver)

  # What HOST_VER_GCC says, unless a test says otherwise: the stack the
  # environment's scope names. Configuration, not scope -- a test that
  # sets it is a test whose version table reads differently.
  def default_stack=(v)
    @default_stack = v
    @graphs = nil          # a package's default version may follow it
  end

  # The package that provides a stack's compiler.
  #
  # Named once, here. Everyone who needs to find the toolchain a stack
  # was built with was spelling "host_gcc" out for themselves, which
  # is one more place to edit the day the stack is built by something
  # that is not GCC -- the schema already leaves room for it.
  def stack_compiler = get("host_gcc")

  # The interpreter our builds run, named once here for the same
  # reason: meson's wrapper, ninja's bootstrap and the $PYTHON token
  # all need it, and three copies of the string "host_python" is
  # three places to edit the day it is not CPython.
  def python_pkg = get("host_python")

  # The packages that exist ONLY to build the host world: our own
  # compiler, the QEMU built with it, and everything nothing else
  # needs -- glibc, the GTK closure, meson, ninja, binutils, the maths
  # libraries GCC wants.
  #
  # Derived from the graph, not tagged package by package. Fifty-odd
  # names is fifty chances to forget one, and the day something Tilck
  # builds starts depending on glib2 the answer has to change by
  # itself rather than by someone noticing.
  #
  # Reachable from a root AND from nothing else. host_zlib would be in
  # the world by the first half and out of it the moment a target
  # package wanted it, which is the behaviour we want.
  def host_world_names
    @host_world ||= compute_host_world_names
  end

  def host_world_roots = all_packages.select(&:host_world_root?)

  def compute_host_world_names

    # Without the Tilck stacks: a stack's members are outside packages
    # of their own, so it adds no edge this derivation needs -- and
    # asking it for its members asks whether each is supported, which
    # asks about the world, which is what is being computed.
    graph = build_dep_graph(stacks: false)
    closure = ->(n) { DepResolver.dep_closure(n, graph) }

    roots = host_world_roots.map(&:name)
    world = roots.flat_map { |r| closure.(r) + [r] }.uniq
    outside = all_packages.map(&:name) - world
    reachable = outside.flat_map { |n| closure.(n) + [n] }.uniq

    return world - reachable
  end

  # WHAT IS INSTALLED (world.rb): the tree, scanned once and held
  # until a writer says it changed. Whatever moves, removes or
  # rewrites an installation calls installs_changed!, and the next
  # question scans again -- once, however often it is asked. What
  # changes the tree from outside this process is not this tool's to
  # notice.
  #
  # TRANSITION (docs/plans/pkgmgr-functional-core.md, step 5.2): a
  # package not bound to a world (Package#at) reads this one, and
  # notes the site. The planner will take a World as an argument.
  def world
    @world = nil if @world && !@world.tc.equal?(TC)   # the tests swap TC
    return @world ||= World.scan(@packages.values, host: env_scope.host)
  end

  def installs_changed!
    @world = nil
  end

  # Scan now rather than at the next question. The callers that
  # announced a change have no need of it; the ones at the top of a
  # mode pay the walk up front, where it was always paid.
  def refresh
    @world = World.scan(@packages.values, host: env_scope.host)
  end

  # Declared default and supported here: the members of this target's
  # Tilck stack, which is what its meta-package depends on.
  def get_default_packages(scope: env_scope)
    return @packages.values.select { |p| p.at(scope).default? }
  end

  # The Tilck stacks, every target's: what the no-mode run installs
  # where it applies, and what the listing opens with.
  def tilck_stacks = @packages.values.select(&:metapackage?)

  def get_upgradable_packages(scope: env_scope)
    return Planner.upgradable(self, world, scope)
  end

  # Installed, but not from the sources we have now: a patch was
  # added, a flag changed, or the code that drives the build did.
  # Reported separately from a version bump because the remedy
  # differs -- a rebuild rather than a new version -- and a package
  # whose version was bumped is left to --upgrade, which rebuilds it
  # anyway, at the new version.
  #
  # Every install, each judged against the recipe as it reads at ITS
  # coordinates -- not the current one's recipe applied to all of
  # them, which reported the whole set as stale from whichever arch
  # happened not to be selected. [package, install] pairs, with the
  # dependencies first: a stale glibc is rebuilt before the gcc that
  # links it.
  def get_stale_installs(scope: env_scope)
    return Planner.stale_installs(self, world.judged(self, scope), scope)
  end

  def get_stale_packages(scope: env_scope)
    return get_stale_installs(scope: scope).map(&:first).uniq
  end

  # Remove exactly what a forced reinstall is about to recreate.
  #
  # -f means "uninstall, then install", so the two halves have to
  # agree about what they cover. Almost every package writes one tree
  # per call and the current coordinates are the whole answer --
  # widening it would delete the riscv64 build of zlib because the
  # user asked to rebuild the i386 one.
  #
  # gnuefi is the exception it has to handle: one call builds i386 AND
  # x86_64, so removing only the current arch left the other in place
  # and the reinstall died on the directory it expected to create:
  #
  #   File exists - .../tilck-x86_64/.../gnuefi/3.0.17/3.0.17
  #
  # (It read as "all versions, current arch" because "ALL" was passed
  # in the `ver` slot of uninstall's positional list, never the arch.)
  def force_remove(name, ver = nil, scope: env_scope)

    pkg = @packages.values.find { |p| p.name == name }

    # An orphan has no package object to ask where it would be
    # rebuilt, so every copy of it goes -- which is what the selector
    # does for an orphan with no version named.
    return uninstall(name, false, false, scope: scope) if pkg.nil?

    # Ask where the install about to run will write, and remove exactly
    # that. Not the arch: an arch covers every board built for it, so a
    # rebuild of the licheerv-nano zlib took the qemu-virt one with it
    # and put nothing back. Nothing reported it either -- a package
    # that is simply gone is "not installed", not "stale" -- and it
    # surfaced two steps later as a riscv64 build that could not find
    # zlib.h.
    # THE version being rebuilt, not every version of the package.
    # Several versions coexist on purpose -- six gcc majors sit side
    # by side in one directory -- and `-s host_gcc:16.2.0 -f` removed
    # all six before building one, so a loop over the majors destroyed
    # each previous build and left whichever was interrupted with
    # none.
    v = ver || pkg.default_ver

    wanted = Planner.coords_of_install_for(pkg, v, scope)

    # Exactly those coordinates, and nothing about the compiler or the
    # arch: the coordinates ARE the compiler and the arch. A version
    # that is not there is not an error -- the selector says so and
    # names nothing, and -f on it is simply an install.
    return uninstall(name, false, false, v, coords: wanted, scope: scope)
  end

  # An install list never holds a candidate (those have no path), so
  # only the package and the version are asked.
  def get_installed_compilers
    world.claimed.select { |x|
      x.pkg&.is_compiler && x.ver == x.target_arch.gcc_ver
    }
  end

  def register(package)

    if !package.is_a?(Package)
      raise ArgumentError
    end

    if @packages.include? package.id
      raise NameError, "package #{package.name} already registered"
    end

    @packages[package.id] = package
    @host_world = nil
    @graphs = nil          # the declared structure grew
    installs_changed!      # the world claims what the registry knows
  end

  # The dependency graph at `scope` (Planner.graph), built once per
  # (scope, stacks) for the life of this registry: forgotten when a
  # package is registered, since the graph is the declared structure
  # and nothing else moves it. A Scope is a value, so it is the key.
  def remembered_graph(scope, stacks)
    @graphs ||= {}
    return @graphs[[scope, stacks]] ||= yield
  end

  def get(name)
    return @packages[name]
  end

  # All registered packages, in registration order. Used by `main.rb`
  # for modes that iterate the full registry (e.g. --list-installable)
  # without going through the per-section filters in show_status_all.
  def all_packages
    return @packages.values
  end

  def get_tc(arch)
    return get(ALL_ARCHS.fetch(arch.to_s).cross_cc_pkg)
  end

  # The default version of a package, from one of the two version
  # files. `host` selects which: host tool versions
  # (other/host_pkg_versions) and target versions (other/pkg_versions)
  # are completely unrelated, so a package that exists on both sides
  # must say which one it means. A few packages legitimately read
  # across: gcc-<arch>-musl is a host package, but the musl version
  # baked into its tarball name is a target one.
  def get_config_ver(name, host:)
    table = host ? @host_config_versions : @config_versions
    return table[name._.upcase]
  end

  def get_smart(pkg_or_name)
    assert { pkg_or_name.is_a? Package or pkg_or_name.is_a? String }
    (pkg_or_name.is_a? Package) ? pkg_or_name : get(pkg_or_name)
  end

  # Resolve a user-supplied package name string, possibly a substring,
  # to a full registered package name. Returns [name, matches] where:
  #
  #   [full_name, nil]  — exact or unique substring match
  #   [nil, []]         — no match at all
  #   [nil, [...]]      — ambiguous; matches ordered by precedence
  #                       (starts_with, then ends_with, then contains)
  #
  # The caller is expected to distinguish exact vs unique substring
  # match itself by comparing the returned name to the input.
  def resolve_name(input)
    # Exact match wins immediately — don't treat it as a substring.
    return [input, nil] if @packages.key?(input)

    all = @packages.keys.select { |n| n.include?(input) }
    return [nil, []] if all.empty?
    return [all[0], nil] if all.length == 1

    # Multiple matches: order by starts_with, then ends_with, then the
    # rest (substring in the middle).
    starts   = all.select { |n| n.start_with?(input) }
    ends     = (all - starts).select { |n| n.end_with?(input) }
    middle   = all - starts - ends
    [nil, starts + ends + middle]
  end

  def with_cc(arch_name, &block)
    arch = ALL_ARCHS.fetch(arch_name)
    arch_gcc = arch.gcc_tc
    arch_dir = Coords.target(arch, arch.default_board, arch.gcc_ver).pkgs_dir
    assert { !arch_gcc.blank? }

    compilers = get_installed_compilers.select { |x| x.target_arch == arch }
    assert { compilers.length == 1 }

    with_saved_env(%w[PATH CC CXX AR NM RANLIB CROSS_PREFIX CROSS_COMPILE]) do

      prepend_to_global_path(compilers[0].path / "bin")
      ENV["CC"]            = "#{arch_gcc}-linux-gcc"
      ENV["CXX"]           = "#{arch_gcc}-linux-g++"
      ENV["AR"]            = "#{arch_gcc}-linux-ar"
      ENV["NM"]            = "#{arch_gcc}-linux-nm"
      ENV["RANLIB"]        = "#{arch_gcc}-linux-ranlib"
      ENV["CROSS_PREFIX"]  = "#{arch_gcc}-linux-"
      ENV["CROSS_COMPILE"] = "#{arch_gcc}-linux-"

      block.call(arch_dir)
    end
  end

  def show_status_all(group_by = nil, all_compilers = false,
                      scope: env_scope)

    curr_cc = scope.arch.gcc_ver
    curr_host_cc = scope.stack

    list_with_paths = world.installs
    by_path = {}

    for info in list_with_paths
      p = info.path
      if (!by_path.include? p) or by_path[p].pkg.nil?
        by_path[p] = info
      end
    end

    # The stacks' meta-packages have a table of their own at the top
    # and would be a line saying nothing in a section.
    installable = @packages.values.flat_map { |p|
      p.at(scope).get_installable_list
    }
    list = (by_path.values() + installable).reject { |x|
      x.pkg&.metapackage?
    }

    # One section per compiler, for host and target alike. Sections
    # are built the same way on both sides; what differs is which
    # compiler counts as the current one -- ARCH's cross compiler for
    # target packages, HOST_VER_GCC's stack for host ones -- and that
    # a version alone does not identify a compiler. Cross GCC 13.3.0
    # and host GCC 13.3.0 are two different programs producing two
    # different sets of binaries, so on_host has to be part of the
    # key or a coincidence of version numbers merges them.
    cc_sections = ->(on_host, current, label) {
      picked = list.select { |x|
        Version === x.compiler && x.on_host == on_host
      }

      picked.map { |x| x.compiler }.uniq.sort.reverse
            .select { |cc| all_compilers || cc == current }
            .map { |cc|
              here = cc == current ? " [ CURRENT ]" : ""
              [
                "#{label} #{cc}#{here}",
                picked.select { |x| x.compiler == cc }
              ]
            }
    }

    # Tilck's own packages before anything of the host's: they are
    # what the toolchain is for. Said in the label too -- a reader saw
    # "Packages built by GCC 13.3.0" under "Host packages built by GCC
    # 14.4.0" and asked which of them Tilck was. Then the host side,
    # all of it labelled as such: the tools the system compiler built,
    # the stacks report, and last the stacks themselves -- a compiler,
    # a libc and a GTK beneath a QEMU, the longest sections and the
    # least often the reason anyone runs -l.
    front = [
      [
        "GCC toolchains",
        list.select { |x| !x.target_arch.nil? }
      ],

      [
        "Source-only packages (noarch)",
        list.select { |x| !x.compiler && !x.arch }
      ],

      *cc_sections.call(false, curr_cc, "Tilck packages built by GCC"),

      [
        "Host packages built by system CC",
        list.select { |x| !x.target_arch and x.compiler.eql? "syscc" }
      ],
    ]

    back = cc_sections.call(true, curr_host_cc, "Host packages built by GCC")
    groups = front + back

    #list.each { |x| puts x }  # DEBUG

    # Sized from the titles themselves: there are now as many host
    # sections as there are stacks, and a fixed width that fitted the
    # old ones left the longest banner sticking out of the row.
    width = groups.map { |msg, _| msg.length }.max

    # A pre-pass, so that the counts are one column down the whole
    # listing and cost nothing when there is nothing to count: how
    # many installs the busiest line stands for decides how wide the
    # count is, and whether there is one at all.
    digits = count_digits(groups)

    # Before dump, which captures it: what each installation needs,
    # what it cannot find, and therefore what cannot be used.
    installs, needs, missing = install_graph
    unusable = unusable_installs(installs, needs, missing)
    cannot_use = unusable.keys.to_set

    # What each record says, asked once: show_status needs it for the
    # stale column and the note below needs it for the count, and
    # reading every .build_inputs twice to answer the same question
    # is the kind of thing -l gets slow by.
    judged = world.judged(self, scope)
    states = installs.to_h { |i|
      [i, i.pkg ? judged.installs.find { |j| j == i }&.record : nil]
    }

    dump = ->(sections) {
      for msg, l in sections do
        next if l.empty?      # a stack with nothing in it is not news
        puts
        puts "--- #{msg.center(width)} ---"
        l.map { |x| x.pkgname }.uniq.each { |pkg|
          show_status(pkg, group_by, l.select { |x| x.pkgname == pkg },
                      digits, unusable: cannot_use, states: states)
        }
      end
    }

    # The stacks, every one with a count, between the host tools and
    # the one stack the listing shows: the listing shows the current
    # stack only, and a reader who saw nothing of a QEMU built into
    # another one asked, reasonably, how they were to know there was
    # more.
    show_tilck_stacks(width: width, needs: needs, scope: scope)
    dump.call(front)
    show_stacks(width: width, needs: needs, scope: scope)
    dump.call(back)

    show_unusable(unusable)
    show_host_libs_changed(states)
    show_old_format(states)

    puts
    puts legend
  end

  # The host stacks themselves: which compilers we have built a world
  # with, and how much of a world is in each.
  #
  # -l grew a section per stack, which is the right answer to "where
  # does this package live" and the wrong one to "which stacks do I
  # have" -- two hundred package lines to count six headings.
  #
  # BUILT means the compiler that names the stack is installed, which
  # is what makes a stack usable at all: everything else in it is
  # built BY that compiler, so without it the directory is either
  # empty or a leftover.
  # The Tilck stacks, one line each: built when its meta-package is
  # installed, with how many packages it holds, and which one this
  # invocation's ARCH and BOARD name.
  def show_tilck_stacks(width: 40, needs: nil, scope: env_scope)

    stacks = tilck_stacks
    return if stacks.empty?
    needs ||= install_graph(scope: scope).last

    puts
    puts "--- #{"Tilck stacks".center(width)} ---"

    for m in stacks do
      inst = world.of(m.name).find { |i| !i.path.nil? && !i.broken }
      held = inst ? held_by([inst], needs).length - 1 : 0
      here = m.at(scope).supported? ? "  [ CURRENT ]" : ""
      # The compiler, as the stack's manifest names it: what the
      # host stacks say in their own column.
      manifest = inst ? StackManifest.read(inst.coords) : nil
      cc = manifest ? "  #{manifest}" : ""
      printf("%-28s %s %3d pkgs%s%s\n", m.name,
             Package.stack_cell(!inst.nil?), held, cc, here)
    end
  end

  def show_stacks(width: 40, needs: nil, scope: env_scope)

    gcc = stack_compiler

    if gcc.nil? || !gcc.at(scope).host_supported?
      puts
      puts "  No host stacks on #{scope.host.machine}: the stack " \
           "(our GCC, our glibc, the QEMU matrix) is built for x86_64 " \
           "Linux only, for now."
      puts
      return
    end

    compilers = world.of(gcc.name).reject { |i| i.path.nil? || i.broken }
    built = compilers.map { |i| StackId.of(i.ver) }

    # Every stack the compiler could define, and every one on disk --
    # a variant or foreign stack among them, listed as it is. A stack
    # is built when its compiler is where its manifest says, whatever
    # this invocation would call that place today; a stack from before
    # the record, when the compiler of its version is installed.
    on_disk = host_stacks(host: scope.host)
    known = (gcc.installable_versions.map { |v| StackId.of(v) } + built +
             on_disk).uniq.sort
    built |= on_disk.select { |id|
      !gcc.at(scope).stack_compiler_dir(id.ver).nil?
    }

    needs ||= install_graph(scope: scope).last

    puts
    puts "--- #{"Host stacks".center(width)} ---"

    # Two numbers a stack: how many packages are in it, and how many
    # of them its compiler holds -- a glibc and a kernel's headers,
    # typically; the rest are there for whatever else was built into
    # the stack, which the next table says.
    for v in known do
      here = v == StackId.of(scope.stack) ? "  [ CURRENT ]" : ""
      cc = compilers.find { |i| StackId.of(i.ver) == v }
      held = cc ? held_in_stack(v, held_by([cc], needs)) : 0
      printf("%-20s %s %3d pkgs, %3d held%s\n",
             v.to_s, Package.stack_cell(built.include?(v)),
             packages_in_stack(v), held, here)
    end

    show_held_tables(width: width, needs: needs)
  end

  # The installs of a stack among `held`, the root itself not counted:
  # what a stack's row and a QEMU's row call "held".
  def held_in_stack(stack, held)
    name = Coords.stack_name(stack)
    return held.count { |i| i.coords.stack == name }
  end

  # A table per package that asks for one (Package#own_table): its
  # installs, each with the stack it is in and how much of that stack
  # it holds. QEMU asks, being the heaviest thing a stack is built
  # for and the reason most of a stack is there.
  def show_held_tables(width:, needs:)

    for pkg in @packages.values do
      title = pkg.own_table
      next if title.nil?
      rows = world.of(pkg.name).reject { |i| i.path.nil? || i.broken }
                .sort_by(&:ver)
      next if rows.empty?

      puts
      puts "--- #{title.center(width)} ---"

      for i in rows do
        stack = i.coords.stack_id
        held = stack ? held_in_stack(stack, held_by([i], needs)) - 1 : 0
        status = Package.installed_str(1, auto: !i.manual)
        printf("%-14s %-13s [ %s ] %3d held\n",
               "#{pkg.pkg_dirname} #{i.ver}", i.coords.stack, status, held)
      end
    end
  end

  # What the colours of the listing say, for the reader who has not
  # learnt them: the two greens above all, since the words are the
  # same.
  def legend
    return "Legend: #{Term.makeGreen('installed')} asked for by name   " \
           "#{Term.makeDarkGreen('installed')} pulled in as a " \
           "dependency\n" \
           "        #{Term.makeYellow('stale')} built from other sources   " \
           "#{Term.makeRed('broken')} incomplete   " \
           "#{Term.makeBlue('found')} no package claims it\n" \
           "        #{Term.makeMagenta('unusable')} a dependency it needs " \
           "is gone"
  end

  # How many packages have been built into one stack. Package
  # directories, not versions: the question is how much of a world is
  # there, and two versions of zlib are still zlib.
  def packages_in_stack(gcc_ver)

    dir = stack_coords(gcc_ver).pkgs_dir
    return 0 if !dir.directory?
    return Dir.children(dir).count { |d| (dir / d).directory? }
  end

  # How many digits the counts in a listing need: zero when no line
  # stands for more than one install, which is the usual case and
  # means no count is printed at all.
  #
  # Counted per LINE, which is one package within one section -- the
  # same grouping show_status is handed -- because that is what the
  # number on the line means. A package with installs in two stacks
  # appears in both, and neither line claims the other's.
  def count_digits(groups)

    max = groups.flat_map { |_, l|
      l.group_by(&:pkgname).values.map { |es|
        es.count { |e| !e.path.nil? && !e.broken }
      }
    }.max || 0

    return max < 2 ? 0 : max.to_s.length
  end

  def show_status(name, group_by, list, digits = 0, unusable: Set.new,
                  states: nil)

    add_braces = ->(s) { "{#{s}}" }

    if list.nil? or list.empty?
      puts "#{name.ljust(35)} [ #{Package.empty_str(digits: digits)} ]"
      return
    end

    if list.all?(&:on_host)
      atos = ->(a) { get_human_arch_name(a) }
    else
      atos = ->(a) { a.nil?? "noarch" : a.name }
    end

    # Split into working installs and broken ones. Only working
    # installs count for the arch/ver display and "installed" status.
    installed = list.filter { |e| !e.path.nil? && !e.broken }
    broken = list.filter { |e| !e.path.nil? && e.broken }

    archs = installed.map{ |e| atos.call(e.arch) }.uniq
    # Sorted: the versions of a package are a sequence, and reading
    # them in the order the filesystem happened to list them --
    # 11.5.0, 13.4.0, 14.4.0, 12.5.0 -- makes a reader check twice.
    # Arches are left in ALL_ARCHS order, which puts the primary one
    # first and is more useful than alphabetical.
    vers = installed.map { |e| e.ver }.uniq.sort

    # A host package's arch is always "host" and a noarch package's
    # always "noarch", so naming it once per version is a column of
    # the same word. Only a target package has an arch worth saying.
    named_arch = !list.all? { |e| e.on_host || e.arch.nil? }

    if group_by.nil?

      s = archs.join(", ")

    elsif group_by == 'arch'

      s = archs.map {
        |a|
        [
          a,
          add_braces.call(
            installed.filter {
              |e| atos.call(e.arch) == a
            }.map(&:ver).uniq.sort.map(&:to_s).join(", ")
          )
        ].join(": ")
      }.join(", ")

    elsif group_by == 'ver'

      s = vers.map {
        |v|
        next v.to_s if !named_arch

        [
          v,
          add_braces.call(
            installed.filter {
              |e| e.ver == v
            }.map(&:arch).uniq.map(&atos).join(", ")
          )
        ].join(": ")
      }.join(", ")

    end

    if list.any? { |x| !x.pkg.nil? }
      if !installed.empty?
        # Present, but built from something other than the current
        # sources. Shown here so the condition is visible without
        # starting a build and discovering it the hard way.
        #
        # Judged at each install's OWN coordinates, exactly as
        # get_stale_packages does. Asking the package for the state of
        # a version re-derives the CURRENT stack, so an install
        # belonging to another one is looked for where it is not,
        # comes back :not_installed, and gets drawn as healthy -- the
        # listing would then disagree with --check-for-updates about
        # the very same install.
        stale = installed.any? { |e|
          next false if e.pkg.nil?
          st = states ? states[e] : e.pkg.at(env_scope, world: world)
                                        .build_inputs_state_of(e)
          [:changed, :old_format, :unknown].include?(st)
        }
        n = installed.length
        auto = installed.none?(&:manual)

        # Unusable before stale: both say "do something", but only
        # this one explains why the package does not work RIGHT NOW,
        # and staleness is what --check-for-updates is for.
        status = if installed.any? { |e| unusable.include?(e) }
          Package.unusable_str(n, digits: digits)
        elsif stale
          Package.stale_str(n, digits: digits)
        else
          Package.installed_str(n, digits: digits, auto: auto)
        end
      elsif !broken.empty?
        status = Package.broken_str(digits: digits)
      else
        status = Package.empty_str(digits: digits)
      end
    else
      if list.any? { |x| !x.path.nil? }
        status = Package.found_str(digits: digits)
      else
        status = Package.empty_str(digits: digits)
      end
    end

    puts "#{name.ljust(35)} [ #{status} ] [ #{s} ]"
  end

  # Install the package
  #
  # param `pkg`:           Package object or name (String).
  #
  # param `ver`:           version of the package to install
  # nil                 => default/auto/configured from ENV
  # other               => might or might not be supported, depending on the
  #                        package. Changes over time. It might not be possible
  #                        to install older versions of the package that were
  #                        supported before
  def install(pkg, ver = nil, default_install: nil, manual: true,
              scope: env_scope)

    name = pkg.is_a?(String) ? pkg : pkg.name
    pkg = get_smart(pkg)
    if !pkg
      error "Package not found: #{name}"
      return false
    end

    if !pkg.enabled?
      error "Package #{pkg.name} is not enabled in this configuration"
      return false
    end

    # Enforce arch_list for regular target packages. Host packages and noarch
    # packages (arch_list == nil) are exempt. We check pkg.default_arch (not
    # ARCH directly) so the filter stays consistent with the InstallInfo
    # produced by regular_target_package_get_installable_list — both use
    # default_arch as the source of truth for "the arch this package builds
    # for in the current invocation context".
    if pkg.target?
      a = pkg.at(scope).default_arch
      if a.nil? || !pkg.arch_list.include?(a)
        a_name = a.nil? ? "<nil>" : a.name
        error "Package #{pkg.name} is not supported for arch #{a_name}"
        return false
      end
    end

    ver = nil if ver.blank?

    # Whether the caller named a version is the only moment this is
    # knowable: from here on, `ver` is a version either way. Deps that
    # the resolver pulled in arrive with ver = nil, which is right —
    # nobody pinned them. A rebuild is the one caller that knows
    # better: it names the version, because the install has one, and
    # says how that install was asked for, because the record does.
    default_install = ver.nil? if default_install.nil?
    ver ||= pkg.at(scope).default_ver

    # One Build, executed, bound to the versions this package resolves
    # as the whole request. Every mode plans whole requests (Planner)
    # and runs them (Executor); this is the entry the tests use.
    bound = Planner.bind(self, [[pkg.name, ver]], scope).first
    action = Build.new(
      name: pkg.name, ver: ver, scope: scope,
      origin: default_install ? :default : :pinned,
      mark: manual ? :manual : :auto, bound: bound,
      against: Planner.against_of(pkg.at(scope), ver, bound)
    )
    return Executor.build(self, action)
  end

  # Build the dependency graph from all registered packages.
  # Returns { "name" => ["dep_name", ...], ... }
  #
  # Target packages (not on_host, has arch_list) implicitly depend on
  # the cross-compiler for the current target_arch, since
  # Package#install_impl calls with_cc() which requires the compiler
  # to be installed. Using target_arch (not ARCH) lets this respect
  # the `-s <pkg> -a <arch>` scope: when installing for a different
  # arch, the dep points at that arch's compiler automatically.
  # stacks: false leaves the Tilck stacks' meta-packages with no
  # dependencies, for the one derivation that must not ask them.
  def build_dep_graph(stacks: true, scope: env_scope)
    return Planner.graph(self, scope, stacks: stacks)
  end

  # Validate the full dependency graph: missing deps + cycle detection.
  # Called once after all packages are registered and before any install.
  def validate_deps
    DepResolver.validate(build_dep_graph)
  end

  # Every registered package must resolve to a version.
  #
  # get_config_ver is a plain hash lookup, so a package whose entry is
  # missing from its version file — a typo, or a new package nobody
  # added a version for — silently gets nil and only surfaces much
  # later, as an empty component in a download URL or an install path.
  # Check it up front and name every offender at once.
  #
  # Raises MissingVersionError listing the packages and the file each
  # one was looked up in.
  def validate_versions

    missing = []
    scope = env_scope

    for pkg in @packages.values
      next if !pkg.at(scope).default_ver.nil?
      fname = pkg.on_host ? "host_pkg_versions" : "pkg_versions"
      missing << "#{pkg.name} (expected in other/#{fname})"
    end

    if !missing.empty?
      raise MissingVersionError,
            "Packages with no version: #{missing.join(', ')}"
    end
  end

  # Names of installations found on disk that no registered package
  # claims — what a package rename or removal leaves behind. `-l`
  # reports these as "found", so `-u` has to be able to name them:
  # uninstall() already handles them via @found_installed, but the
  # CLI's name resolution only knows registered packages.
  def orphan_names
    return orphan_installs.map { |x| x.pkgname }.uniq
  end

  # What the scan found that no package claims: on disk, unowned.
  def orphan_installs = world.orphans

  # The version each package in `name`'s closure resolves to, with
  # `ver` as the version of `name` itself (nil = its default).
  #
  # An explicit pin displaces a default, and says so at info level: a
  # default quietly not being used is exactly the kind of thing worth
  # seeing in the log.
  def resolved_versions(name, ver = nil, scope: env_scope)
    return resolved_versions_for([[name, ver]], scope: scope)
  end

  # Same, for several requested packages at once. They must be resolved
  # together, not one at a time and merged: two of them pinning the
  # same dependency to different versions is a conflict, and merging
  # per-root results would silently let the last one win.
  def resolved_versions_for(pairs, scope: env_scope)
    bound, notes = Planner.bind(self, pairs, scope)
    notes.each { |n| info n }
    return bound
  end

  # The host stack's root, and the sysroot inside it. Defined here
  # rather than on Package because several packages, the sysroot
  # composition and the audit all need the same answer.
  # The coordinates of one of OUR stacks: needs nothing from the
  # machine, built by the compiler named -- a StackId, or the version
  # of the plain gcc stack.
  def stack_coords(stack = nil, host: env_scope.host)

    stack ||= default_stack_cc_ver

    if stack.nil?
      raise "HOST_VER_GCC is missing from other/host_pkg_versions: it " \
            "names the host stack's directory, so without it every " \
            "stack package would install to the same broken path"
    end

    return Coords.new(host.machine, nil, Coords.stack_name(stack))
  end

  def stack_root(stack = nil) = stack_coords(stack).root
  def stack_sysroot(stack = nil) = stack_coords(stack).sysroot

  # Which stack the world is currently being built for. Everything
  # except host_gcc belongs to this one: choosing a different compiler
  # means changing HOST_VER_GCC and rebuilding, which is a coherent
  # operation. host_gcc is the exception, because a compiler has to
  # belong to ITS OWN stack — see HostGccPackage#stack_gcc_ver.
  def default_stack_cc_ver
    return @default_stack || get_config_ver("gcc", host: true)
  end

  # The stacks that exist on disk, as StackIds, sorted: every
  # directory under <host>/any spelled like a stack, gcc-14.4.0-lto
  # and clang-18.1.0 included. What is NOT a stack is left alone.
  #
  # Reading directories here is not the disambiguation toolchain4
  # needed. There, one level held both packages and compilers and a
  # name had to be parsed to tell them apart. Here the level holds
  # nothing but stacks, and the only question is which spelling each
  # one has.
  def host_stacks(host: env_scope.host)

    machine = host.machine
    dir = TC / machine / Coords::ANY
    return [] if !dir.directory?

    # stack_id, not parse_stack: this reads a DIRECTORY, and a
    # directory is only a stack if it is spelled like one. parse_stack
    # is lenient because it reads what a person typed.
    return Dir.children(dir)
              .filter_map { |d| Coords.new(machine, nil, d).stack_id }
              .sort
  end

  # readelf and the dynamic loader the audit uses. Ours when we have
  # them — we build binutils, so readelf is a tool we own — falling
  # back to the system readelf, which reads the same ELF either way.
  # Without our loader there is no resolution check, and the audit
  # reports that rather than passing silently.
  def audit_tools(gcc_ver = nil)

    bu = get("host_binutils")&.at(env_scope, world: world)
    bu_inst = bu&.find_install(bu.default_ver)
    readelf = bu_inst ? bu_inst.path / "install/bin/readelf" : "readelf"

    # The loader of the stack being audited, not of whichever stack
    # happens to be the default; where this host's glibc puts it.
    loader = stack_sysroot(gcc_ver) / env_scope.host.abi.loader
    loader = nil if !File.exist?(loader)

    return [readelf, loader]
  end

  # Check that an installed stack package references nothing outside
  # the toolchain. Returns true when clean.
  #
  # Only stack packages are audited. binutils and gcc are :distro
  # and link the system libc by design: they are build tools, and what
  # they link against never reaches what they produce.
  def audit_portability(pkg, ver)

    inst = pkg.find_install(ver)
    return true if inst.nil?

    readelf, loader = audit_tools(pkg.stack_gcc_ver(ver))
    if loader.nil?
      warning "No stack loader yet: auditing #{pkg.name} without " \
              "checking where its libraries resolve"
    end

    hostile = pkg.portability_hostile_check?
    if !hostile
      info "#{pkg.name}: auditing without a hostile LD_LIBRARY_PATH " \
           "(see Package#portability_hostile_check?)"
    end

    violations = Portability.audit(
      inst.path, allowed: [TC], abi: env_scope.host.abi, readelf: readelf,
      loader: loader, hostile: hostile
    )

    return true if violations.empty?

    error "#{pkg.name} #{ver} is not portable:"
    for v in violations.first(20)
      error "  #{v}"
    end
    if violations.length > 20
      error "  ... and #{violations.length - 20} more"
    end

    return false
  end

  # Rebuild the sysroot from every installed stack package, each at
  # the version currently selected for it.
  #
  # Run after any portable install, because a package that bakes an
  # absolute --prefix naming the sysroot is broken until the sysroot
  # makes that path real: glibc's own libc.so.6 will not exec before
  # this has run, its ELF interpreter pointing into a directory that
  # does not exist yet.
  def compose_stack_sysroot(stack = nil)

    stack = StackId.coerce(stack || default_stack_cc_ver)

    # A stack the tool cannot build into, it cannot compose either:
    # what its packages need is asked at a scope, and the scope's
    # stack is a plain gcc one. Said, and left as it is.
    if !stack.plain?
      warning "Not composing the sysroot of #{stack}: only plain gcc " \
              "stacks can be built into, so far"
      return 0
    end

    gcc_ver = stack.ver
    at_stack = env_scope.with(stack: gcc_ver)
    fragments = @packages.values.flat_map { |p|
      p.at(at_stack, world: world).sysroot_fragments(gcc_ver)
    }

    # No fragments has two very different causes, and only one of
    # them is a bug.
    #
    #   the stack HAS packages installed -> we asked the wrong
    #     question and are about to replace a working sysroot with
    #     nothing. That is how five of them were emptied at once.
    #
    #   the stack has NO packages -> emptying is exactly right, and
    #     refusing leaves a farm of symlinks pointing at things that
    #     were just uninstalled. --clean hit precisely this.
    #
    # So look at the tree rather than at the fragment count alone.
    root = stack_sysroot(gcc_ver)
    pkgs = stack_coords(gcc_ver).pkgs_dir
    has_pkgs = pkgs.directory? && !Dir.empty?(pkgs.to_s)

    if fragments.empty? && has_pkgs
      error "Refusing to empty the composed sysroot of #{stack}: " \
            "it has packages installed, so finding no fragments for it " \
            "means the question was asked wrongly, not that it is empty"
      return 0
    end

    n = Sysroot.compose(root, fragments)
    info "Composed sysroot #{stack}: #{n} entries from " \
         "#{fragments.length} packages"
    return n
  end

  # The mark an upgrade inherits (Planner.inherited_mark).
  def upgrade_inherits_manual?(pkg)
    return Planner.inherited_mark(world, pkg.name) == :manual
  end

  # The installations one install needs (Planner.needs_of).
  def needs_of_install(pkg, inst, scope: env_scope)
    return Planner.needs_of(self, world, inst, scope)
  end

  # Where an install of `pkg` at `ver` would write, from the current
  # scope: the -f question, asked for a dependency.
  def coords_of_install_for(pkg, ver, scope: env_scope)
    return Planner.coords_of_install_for(pkg, ver, scope)
  end

  # Every installation, and what each one needs among them
  # (Planner.install_graph): what --autoremove walks and the listing
  # counts.
  def install_graph(scope: env_scope)
    return Planner.install_graph(self, world, scope)
  end

  # Installations that cannot be used, and what they are waiting for
  # (Planner.unusable): {install => [words]}.
  def unusable_installs(installs, needs, missing)
    return Planner.unusable(installs, needs, missing)
  end

  #
  # A record written by an older digest scheme holds a number this one
  # cannot produce. Those installs read stale, and saying only that
  # would be the listing blaming the sources for something the
  # fingerprint did -- on a toolchain built before the conversion,
  # that is most of the tree.
  #
  def show_old_format(states)

    n = states.count { |_, s| s == :old_format }
    return if n.zero?

    puts
    puts "#{n} install(s) carry a record from an older recipe format, " \
         "so their"
    puts "digests cannot be compared. They read stale for that reason, " \
         "not because"
    puts "their sources changed."
  end

  # An install that reads changed because the host's libraries moved
  # under it, with the libraries named: the status cell says
  # "changed" for a recipe change too, and the remedy is the same
  # (--rebuild), but the reader should know it was the distro that
  # moved, not the sources.
  def show_host_libs_changed(states)

    moved = states.filter_map { |i, s|
      next nil if s != :changed
      libs = BuildInputs.syslibs_changed(i.path)
      next nil if libs.empty?
      [i, libs]
    }
    return if moved.empty?

    puts
    puts "Changed under them: the host's libraries these were built " \
         "against have"
    puts "moved (--rebuild builds them against what is there now):"

    moved.sort_by { |i, _| [i.pkgname, i.ver.to_s] }.each { |i, libs|
      name = "#{i.pkgname} #{i.ver}"
      words = libs.map { |p, how| "#{File.basename(p)} (#{how})" }
      puts "    #{name.ljust(34)}#{words.join(", ")}"
    }
  end

  # The list under the table: the status cell has room for the word
  # and none for the reason, and the reason is the actionable half.
  def show_unusable(unusable)

    return if unusable.empty?

    puts
    puts "Unusable: built correctly, but something they need is gone:"

    unusable.sort_by { |i, _| [i.pkgname, i.ver.to_s] }.each { |i, why|
      name = "#{i.pkgname} #{i.ver}"
      puts "    #{name.ljust(34)}needs #{why.join(", ")}"
    }
  end

  # What `roots` hold: everything they need, transitively.
  def held_by(roots, needs) = Planner.held_by(roots, needs)

  def say_removals(plan, dry)
    p = "[DRY RUN] " if dry
    for r in plan.removes do
      i = r.install
      puts "#{p}Remove pkg '#{i.pkgname}' install at #{i.path}"
    end
  end

  # Replace an install with a fresh build of the same version
  # (Executor.replace); an install that is not there is an install.
  def replace(pkg, ver, default_install:, manual: true, scope: env_scope)

    inst = pkg.at(scope, world: world).find_install(ver)
    if inst.nil?
      return install(pkg, ver, default_install: default_install,
                               manual: manual, scope: scope)
    end

    bound = Planner.bind(self, [[pkg.name, ver]], scope).first
    build = Build.new(name: pkg.name, ver: ver, scope: scope,
                      origin: default_install ? :default : :pinned,
                      mark: manual ? :manual : :auto, bound: bound,
                      against: Planner.against_of(pkg.at(scope), ver, bound))
    return Executor.replace(self, Replace.new(install: inst, build: build))
  end

  # The version of each dependency `pkg` at `ver` is built against:
  # the one the request resolved, else the dependency's own pin, else
  # its default. What InstallRecord records as `against`.
  def built_against(pkg, ver, scope: env_scope)
    bound = Planner.bind(self, [[pkg.name, ver]], scope).first
    return Planner.against_of(pkg.at(scope), ver, bound)
  end

  # What an install was built against (Planner.deps_of_install):
  # [versions, ambiguous].
  def deps_of_install(pkg, inst, scope: env_scope)
    return Planner.deps_of_install(self, world, pkg, inst, scope)
  end

  def dep_closure(name, scope: env_scope)
    return DepResolver.dep_closure(name, build_dep_graph(scope: scope))
  end

  # TRANSITION: the install plan as the tests still read it -- [name,
  # ver] pairs in build order, the version nil where the default is
  # meant -- computed by the Planner.
  #
  # Raises the solver's errors, as it always did.
  def resolve_install_plan(requested_pairs, scope: env_scope)

    Planner.bind(self, requested_pairs, scope)   # raises on a conflict
    plan = Planner.plan_install(self, world, requested_pairs, scope)
    raise plan.message if plan.is_a?(Refusal)

    plan.notes.each { |n| info n }

    # A version the user named binds as a pin, so it is :pinned and
    # comes back as itself; nothing else needs saying about it.
    return plan.builds.map { |b|
      [b.name, b.origin == :pinned ? b.ver : nil]
    }
  end

  # Uninstall the package
  #
  # param `pkg_or_name`:   package object or package name to uninstall.
  # param `dry`:           dry-run when it's true
  # param `force`:         include compilers in "ALL"
  #
  # param `ver`:           version of the package to uninstall
  # nil                 => default/auto/configured from ENV (like install())
  # '*'                 => uninstall all versions found (for the given
  #                        compiler)
  # other               => uninstall a specific version, if exists.
  #
  # param `compiler`:      version of compiler used to build the package:
  #                        a specific version of the compiler, might have
  #                        multiple versions of the same package. The same
  #                        package version, might exist for multiple compilers.
  #
  # nil                 => default/auto/configured from ENV
  # '*'                 => all compiler versions
  # other               => "syscc" or compiler version (e.g. Ver("12.4.0"))
  #
  # param `arch`:          target architecture of the package to uninstall:
  #                        each package might have been built using multiple
  #                        compiler versions, for multiple target architectures
  #                        in multiple different versions.
  # nil                 => default/auto/configured from ENV (like install())
  # '*'                 => all architectures
  # other               => specific architecture (e.g. i386)
  # `coords`, when given, restricts the selection to installations at
  # exactly those coordinates. The other filters cannot express a board:
  # `arch` matches every board of that arch at once, which is right for
  # `-u <pkg> -a riscv64` and wrong for a forced rebuild, where it
  # deleted the qemu-virt zlib on the way to reinstalling the
  # licheerv-nano one.
  #
  # Packages this may NEVER remove, whatever it is asked.
  #
  # Ruby is the interpreter running this code. Removing it is not a
  # bold choice the user gets to make with the right flags: it is the
  # package manager deleting itself in the middle of a job, leaving a
  # tree only the bash bootstrap can recover. It is not ours in the
  # first place -- the bootstrap installs it, before any of this runs.
  #
  # Not a `clean` policy but an invariant of uninstall, because the
  # expression that reaches it does not matter: `-u ruby`,
  # `-u ALL -f -a ALL -c ALL`, and anything else all have to stop
  # here.
  #
  # The cache needs no rule. It lives beside the installs rather than
  # inside them, and nothing here walks anywhere but <coords>/pkgs/.
  #
  NEVER_REMOVE = Planner::NEVER_REMOVE

  #
  # Everything, except what a clean must never take.
  #
  # The prebuilt cross-compilers stay because they are downloaded
  # blobs, not built here, and nothing about them can go stale in a
  # way a rebuild would fix. Ruby and the cache stay by the rule
  # above.
  #
  # `except` names packages to leave alone. --clean itself passes
  # none; the system tests pass the host world, because wiping a GCC
  # and a GTK-enabled QEMU to prove that busybox builds is hours of
  # rebuilding for a question neither answers.
  # --clean: what Planner.plan_clean says, printed, then run unless dry.
  def clean(dry, except: [], force: false, scope: env_scope)
    plan = Planner.plan_clean(self, world, scope, except: except,
                              force: force)
    plan.notes.each { |n| warning n }
    say_removals(plan, dry)
    Executor.run(self, plan) if !dry
    return plan.removes.length
  end

  # WHICH installations `-u` means, as one value (Planner.selector).
  # Returns nil, having said why, when a named version is not there.
  def uninstall_selector(pkg, name, install_list, ver: nil, compiler: nil,
                         arch: nil, board: nil, coords: nil,
                         scope: env_scope)
    sel = Planner.selector(self, pkg, name, install_list, scope, ver: ver,
                           compiler: compiler, arch: arch, board: board,
                           coords: coords)
    if sel.is_a?(Refusal)
      warning sel.message
      return nil
    end
    return sel
  end

  # The coordinates an uninstall of `pkg` is about (Planner.uninstall_where).
  def uninstall_where(pkg, all_pkgs, cc, arch, board = nil, scope: env_scope)
    return Planner.uninstall_where(self, pkg, all_pkgs, cc, arch, board,
                                   scope)
  end

  # Uninstall: what Planner.plan_uninstall says, printed, then run
  # (Executor) unless `dry`. See the planner for what ver, compiler,
  # arch and coords mean. Returns how many were taken -- or, in a dry
  # run, how many would be: a caller that reports "removed nothing"
  # when it selected fifty is worse than one that says nothing at all.
  def uninstall(pkg_or_name, dry, force, ver = nil, compiler = nil,
                arch = nil, board: nil, coords: nil, except: [],
                scope: env_scope)

    if pkg_or_name.blank?
      raise ArgumentError, "Invalid package name: '#{pkg_or_name}'"
    end

    name = pkg_or_name.is_a?(Package) ? pkg_or_name.name : pkg_or_name
    plan = Planner.plan_uninstall(self, world, name, scope, ver: ver,
                                  compiler: compiler, arch: arch,
                                  board: board, coords: coords,
                                  force: force, except: except)

    plan.notes.each { |n| warning n }
    say_removals(plan, dry)
    Executor.run(self, plan) if !dry
    return plan.removes.length
  end

  private

  # Read one of the two version files into { "BUSYBOX" => Version }.
  # `prefix` is the key prefix that file uses (VER_ or HOST_VER_); it is
  # required on every entry and stripped from the resulting keys, so the
  # two tables are looked up by bare package name.
  def read_config_versions(fname, prefix)

    result = {}
    data = File.read(MAIN_DIR / "other" / fname)

    for line in data.split("\n")
      next if line.blank? || line.start_with?("#")

      if !line.start_with? prefix
        raise "Invalid line in #{fname}: #{line}"
      end

      line = line.sub(prefix, "")
      key, value = line.split("=")

      if key.blank? || value.blank?
        raise "Invalid line in #{fname}: #{line}"
      end

      if result[key]
        raise "Duplicate key in #{fname}: #{key}"
      end

      result[key] = Ver(value)
    end

    return result
  end

end # Class PackageManager

def pkgmgr = PackageManager.instance
