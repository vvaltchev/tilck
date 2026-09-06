# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'package'
require_relative 'dep_resolver'
require_relative 'version_solver'
require_relative 'sysroot'
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
    @known_pkgs_paths = nil
    @known_installed = nil
    @found_installed = nil
    @installable = nil
    @resolved_versions = nil
    @target_arch = nil    # nil = fall back to the global ARCH
    @portable_stack = nil # nil = fall back to HOST_VER_GCC
  end

  # Current target architecture: the arch the install/uninstall flow
  # is currently operating on. Defaults to the global ARCH constant;
  # temporarily overridden by with_target_arch { ... } to honor the
  # `-a <arch>` CLI flag in `-s` mode (i.e. "install this package for
  # a different arch than ARCH"). Every arch-sensitive computation
  # in the install/introspection path reads this instead of ARCH
  # directly, so the override flows transparently through:
  #   - Package#default_arch / default_cc / arch_supported?
  #   - Package#final_install_root (→ the install dir)
  #   - PackageManager#build_dep_graph (→ the implicit compiler dep)
  #   - ALL-expansion for -s ALL (→ per-arch installable set)
  def target_arch
    @target_arch || ARCH
  end

  # Run `block` with the target arch temporarily set to `arch`. Nests
  # correctly — the previous @target_arch (possibly another override,
  # possibly nil) is saved on entry and restored on exit even if the
  # block raises. The block's return value is propagated.
  def with_target_arch(arch, &block)
    assert { arch.is_a?(Architecture) }
    prev = @target_arch
    @target_arch = arch
    begin
      return block.call
    ensure
      @target_arch = prev
    end
  end

  # The host stack this invocation is building into.
  #
  # Scoped, the same way with_target_arch scopes the target
  # architecture. `-s host_gcc:13.4.0` builds the 13.4.0 stack — its
  # kernel headers, its glibc, then the compiler — because asking for a
  # version is a request to BUILD that version.
  #
  # HOST_VER_GCC is a convenience only: it supplies a version when none
  # is named, so `-s host_gcc` works. It never limits which versions
  # can be built or coexist.
  def with_host_stack(gcc_ver, &block)

    prev = @portable_stack
    @portable_stack = gcc_ver

    begin
      return block.call
    ensure
      @portable_stack = prev
    end
  end

  # The stack in effect: what this invocation asked for, else the
  # default.
  def current_host_stack
    return @portable_stack || default_stack_cc_ver
  end

  # Set the stack for the whole invocation, which is what -H does.
  # with_host_stack is for internal work that looks at another stack
  # and has to put this one back; a choice made on the command line
  # has no "back" to return to.
  def host_stack=(gcc_ver)
    @portable_stack = gcc_ver
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

    roots = all_packages.select(&:host_world_root?).map(&:name)
    return [] if roots.empty?

    world = roots.flat_map { |r| dep_closure(r) + [r] }.uniq
    outside = all_packages.map(&:name) - world
    reachable = outside.flat_map { |n| dep_closure(n) + [n] }.uniq

    return world - reachable
  end

  def python_interpreter

    pkg = python_pkg
    inst = pkg&.find_install(pkg.default_ver)

    raise "host_python is not installed: there is no interpreter to " \
          "run the build with" if inst.nil?

    return inst.path / "bin" / "python3"
  end

  def refresh
    @known_pkgs_paths = Set.new()
    @known_installed = []
    @installable = []

    for pkg in @packages.values do
      sublist = pkg.get_install_list()
      @known_pkgs_paths += sublist.map { |x| x.path }
      @known_installed += sublist
      @installable += pkg.get_installable_list()
    end

    @found_installed = scan_toolchain()
  end

  def get_default_packages
    @packages.values.select(&:default?)
  end

  def get_upgradable_packages
    @packages.values.select { |p|
      p.host_supported? && p.board_supported? && p.arch_supported? &&
      p.needs_upgrade?
    }
  end

  # Installed, but not from the sources we have now: a patch was
  # added, a flag changed, or the code that drives the build did.
  # Reported separately from a version bump because the remedy
  # differs -- a rebuild rather than a new version.
  def get_stale_packages
    @packages.values.select { |p|
      next false if !(p.host_supported? && p.board_supported? &&
                      p.arch_supported?)
      # Every install, each judged against the recipe as it reads at
      # ITS coordinates -- not the current one's recipe applied to all
      # of them, which reported the whole set as stale from whichever
      # arch happened not to be selected.
      p.get_install_list.any? { |i|
        !i.path.nil? && !i.broken &&
        [:changed, :unknown].include?(p.build_inputs_state_of(i))
      }
    }
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
  def force_remove(name, ver = nil)

    pkg = @packages.values.find { |p| p.name == name }

    # An orphan has no package object to ask, so fall back to the
    # plain behaviour: every version at the current coordinates.
    return uninstall(name, false, false, "ALL") if pkg.nil?

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

    wanted = pkg.install_archs(v).map { |a|
      a ? with_target_arch(a) { pkg.coords(v) } : pkg.coords(v)
    }

    # Nothing installed at those coordinates is not an error: -f on a
    # version that is not there yet is simply an install. Checked here
    # rather than left to uninstall, whose "the asked-for version is
    # not installed, so remove whatever is" fallback is right for a
    # user typing -u and catastrophic for this.
    present = pkg.get_install_list.any? { |i|
      i.ver == v && !i.path.nil? && wanted.include?(i.coords)
    }

    return true if !present

    # "ALL" for the compiler, not nil. nil does not mean "any": the
    # filter reads it as "the compiler must BE nil", which is true
    # only of a noarch package -- so a forced rebuild of anything
    # else removed nothing, found its own install still there, and
    # said "already installed" after announcing the removal. The
    # coordinates are what narrows this; for a stack package they ARE
    # the compiler.
    return uninstall(name, false, false, v, "ALL", "ALL", coords: wanted)
  end

  def get_installed_compilers
    @known_installed.select { |x|
      !x.pkg.nil? && x.pkg.is_compiler && !x.path.nil? &&
      x.ver == x.target_arch.gcc_ver
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
    return get("gcc-#{arch}-musl")
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

  def with_cc(arch_name = nil, &block)
    arch = arch_name ? ALL_ARCHS[arch_name] : ARCH
    arch_gcc = arch.gcc_tc
    arch_dir = Coords.new("tilck-#{arch.name}", arch.default_board,
                          "gcc-#{arch.gcc_ver}").pkgs_dir
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

  def show_status_all(group_by = nil, all_compilers = false)

    curr_cc = ARCH.gcc_ver
    curr_host_cc = current_host_stack

    list_with_paths = @known_installed + @found_installed
    by_path = {}

    for info in list_with_paths
      p = info.path
      if (!by_path.include? p) or by_path[p].pkg.nil?
        by_path[p] = info
      end
    end

    list = by_path.values() + @installable

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

    groups = [
      [
        "GCC toolchains",
        list.select { |x| !x.target_arch.nil? }
      ],

      [
        "Packages built by system CC",
        list.select { |x| !x.target_arch and x.compiler.eql? "syscc" }
      ],

      [
        "Source-only packages (noarch)",
        list.select { |x| !x.compiler && !x.arch }
      ],

      *cc_sections.call(true, curr_host_cc, "Host packages built by GCC"),
      *cc_sections.call(false, curr_cc, "Packages built by GCC"),
    ]

    #list.each { |x| puts x }  # DEBUG

    # Sized from the titles themselves: there are now as many host
    # sections as there are stacks, and a fixed width that fitted the
    # old ones left the longest banner sticking out of the row.
    width = groups.map { |msg, _| msg.length }.max
    banner = ->(s) { puts; puts "--- #{s.center(width)} ---" }

    # A pre-pass, so that the counts are one column down the whole
    # listing and cost nothing when there is nothing to count: how
    # many installs the busiest line stands for decides how wide the
    # count is, and whether there is one at all.
    digits = count_digits(groups)

    for msg, l in groups do
      next if l.empty?      # a stack with nothing in it is not news
      banner.call msg
      l.map { |x| x.pkgname }.uniq.each { |pkg|
        show_status(pkg, group_by, l.select { |x| x.pkgname == pkg }, digits)
      }
    end

    puts
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
  def show_stacks

    gcc = stack_compiler
    built = gcc.get_install_list
               .reject { |i| i.path.nil? || i.broken }
               .map(&:ver)

    known = (gcc.installable_versions + built +
             host_stacks.map { |v| Ver(v) }).uniq.sort

    puts
    puts "--- #{"Host stacks".center(40)} ---"

    for v in known do
      status = built.include?(v) ? Package::BUILT_STR : Package::NOT_BUILT_STR
      here = v == current_host_stack ? "  [ CURRENT ]" : ""
      printf("%-20s [ %s ] %3d pkgs%s\n",
             Coords.stack_name(v), status, packages_in_stack(v), here)
    end

    puts
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

  def show_status(name, group_by, list, digits = 0)

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
          e.pkg &&
          [:changed, :unknown].include?(e.pkg.build_inputs_state_of(e))
        }
        n = installed.length
        status = stale ? Package.stale_str(n, digits: digits)
                       : Package.installed_str(n, digits: digits)
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
  def install(pkg, ver = nil)

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
    if !pkg.on_host && !pkg.arch_list.nil?
      a = pkg.default_arch
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
    # nobody pinned them.
    default_install = ver.nil?

    ver ||= pkg.default_ver()
    ok = pkg.install_impl(ver)
    if ok
      # Record how this version was chosen, so --upgrade can leave
      # pinned versions alone. Written after the install rather than
      # inside it, so all three install_impl overrides get it for free.
      inst = pkg.find_install(ver)

      if inst
        InstallOrigin.write(inst.path, default_install)
      end

      # ...and what it was built FROM, in the same place and for the
      # same reason: nothing else on disk can answer it, and without
      # it "installed" means only that a directory exists.
      #
      # Every install of this version, not just the first: gnuefi
      # builds for i386, x86_64 AND noarch from one call, and
      # recording only what find_install happened to return left two
      # thirds of it unverifiable.
      for a in pkg.install_archs(ver)
        i = a ? with_target_arch(a) { pkg.find_install(ver) }
              : pkg.find_install(ver)
        pkg.write_build_inputs(i) if i
      end

      # The sysroot is a view over what is installed, so it is stale
      # the moment that changes.
      # Recompose whenever the package contributes to the sysroot, which
      # is not the same as being a stack package: host_gcc is
      # :distro and still contributes its target runtime, while a
      # portable APPLICATION contributes nothing at all because nothing
      # is built against it.
      # The stack this install belongs to, which for host_gcc is its
      # OWN version rather than the current default.
      stack = pkg.stack_gcc_ver(ver)

      if !pkg.sysroot_fragments(stack).empty?
        compose_stack_sysroot(stack)

        # Now that the sysroot includes this package, let it check
        # whatever it could not check before.
        ok = false if !pkg.post_sysroot_check(stack)
      end

      # Audited on being portable, not on contributing to the sysroot:
      # an application is exactly the thing whose linkage matters most,
      # and it contributes nothing. Runs after any composition, since a
      # package whose paths name the sysroot cannot be inspected until
      # the sysroot is real. binutils and gcc are :distro and link the
      # system libc by design, so they are not audited.
      if pkg.host_tier == :stack
        ok = false if !audit_portability(pkg, ver)
      end

      info "Installed package #{pkg.name} at version #{ver}"
      # Refresh cached install lists so with_cc() can find a
      # just-installed compiler when subsequent packages need it.
      refresh() if pkg.is_compiler
    end
    return ok.nil?? true : ok
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
  def build_dep_graph
    cc_name = "gcc-#{target_arch.name}-musl"
    has_cc = @packages.key?(cc_name)

    @packages.transform_values { |pkg|
      deps = pkg.dep_list.map { |d| d.name }
      if has_cc && !pkg.on_host && !pkg.arch_list.nil?
        deps << cc_name if !deps.include?(cc_name)
      end
      deps
    }
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

    for pkg in @packages.values
      next if !pkg.default_ver.nil?
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
    return (@found_installed || []).map { |x| x.pkgname }.uniq
  end

  # The version each package in `name`'s closure resolves to, with
  # `ver` as the version of `name` itself (nil = its default).
  #
  # An explicit pin displaces a default, and says so at info level: a
  # default quietly not being used is exactly the kind of thing worth
  # seeing in the log.
  def resolved_versions(name, ver = nil)
    return resolved_versions_for([[name, ver]])
  end

  # Same, for several requested packages at once. They must be resolved
  # together, not one at a time and merged: two of them pinning the
  # same dependency to different versions is a conflict, and merging
  # per-root results would silently let the last one win.
  def resolved_versions_for(pairs)

    return VersionSolver.resolve(
      pairs,
      deps_of: ->(n, v) {
        pkg = get(n)
        pkg ? pkg.check_dep_pins(pkg.dep_list_for(v)) : []
      },
      default_of: ->(n) { get(n)&.default_ver },
      on_override: ->(n, default_ver, pinned, path) {
        info "#{n}: using #{pinned}, not the default #{default_ver} " \
             "(pinned via #{path.join(' -> ')})"
      },
    )
  end

  # The host stack's root, and the sysroot inside it. Defined here
  # rather than on Package because several packages, the sysroot
  # composition and the audit all need the same answer.
  # The coordinates of one of OUR stacks: needs nothing from the
  # machine, built by the compiler named.
  def stack_coords(gcc_ver = nil)

    gcc_ver ||= default_stack_cc_ver

    if gcc_ver.nil?
      raise "HOST_VER_GCC is missing from other/host_pkg_versions: it " \
            "names the host stack's directory, so without it every " \
            "stack package would install to the same broken path"
    end

    return Coords.new(HOST_OS_ARCH, nil, Coords.stack_name(gcc_ver))
  end

  def stack_root(gcc_ver = nil) = stack_coords(gcc_ver).root
  def stack_sysroot(gcc_ver = nil) = stack_coords(gcc_ver).sysroot

  # Which stack the world is currently being built for. Everything
  # except host_gcc belongs to this one: choosing a different compiler
  # means changing HOST_VER_GCC and rebuilding, which is a coherent
  # operation. host_gcc is the exception, because a compiler has to
  # belong to ITS OWN stack — see HostGccPackage#stack_gcc_ver.
  def default_stack_cc_ver = get_config_ver("gcc", host: true)

  # The GCC versions of our stacks that exist on disk.
  #
  # Returns versions rather than stack ids, because that is what the
  # callers want: which compilers have a stack built with them.
  #
  # Selecting gcc-* here is NOT the disambiguation toolchain4 needed.
  # There, one directory level held both packages and compilers and a
  # name had to be parsed to tell them apart. Here the level holds
  # nothing but stacks, so this is only asking which of them are gcc
  # ones -- a stack may legitimately be called anything, and the
  # schema leaves room for gcc-14.4.0-lto or a clang stack later.
  def host_stacks

    dir = TC / HOST_OS_ARCH / Coords::ANY
    return [] if !dir.directory?

    # stack_ver, not parse_stack: this reads a DIRECTORY, and a
    # directory is only a stack if it is spelled like one. parse_stack
    # is lenient because it reads what a person typed.
    return Dir.children(dir)
              .filter_map { |d| Coords.new(HOST_OS_ARCH, nil, d).stack_ver }
              .map(&:to_s)
              .sort
  end

  # readelf and the dynamic loader the audit uses. Ours when we have
  # them — we build binutils, so readelf is a tool we own — falling
  # back to the system readelf, which reads the same ELF either way.
  # Without our loader there is no resolution check, and the audit
  # reports that rather than passing silently.
  def audit_tools(gcc_ver = nil)

    bu = get("host_binutils")
    bu_inst = bu&.find_install(bu.default_ver)
    readelf = bu_inst ? bu_inst.path / "install/bin/readelf" : "readelf"

    # The loader of the stack being audited, not of whichever stack
    # happens to be the default.
    loader = stack_sysroot(gcc_ver) / "usr/lib/ld-linux-x86-64.so.2"
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
      inst.path, allowed: [TC], readelf: readelf, loader: loader,
      hostile: hostile
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
  def compose_stack_sysroot(gcc_ver = nil)

    gcc_ver ||= default_stack_cc_ver
    fragments = @packages.values.flat_map { |p| p.sysroot_fragments(gcc_ver) }

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
      error "Refusing to empty the composed sysroot of gcc-#{gcc_ver}: " \
            "it has packages installed, so finding no fragments for it " \
            "means the question was asked wrongly, not that it is empty"
      return 0
    end

    n = Sysroot.compose(root, fragments)
    info "Composed sysroot gcc-#{gcc_ver}: #{n} entries from " \
         "#{fragments.length} packages"
    return n
  end

  # Transitive dependency closure of `name`, nearest dependency first.
  # Used by Package#deps_build_env to collect the build interfaces a
  # package's dependencies publish.
  # The version a package was resolved to for the request being
  # installed right now, or nil outside an install.
  #
  # Set by resolve_install_plan, which is the only place that knows
  # the whole closure and therefore the only place that can answer.
  def resolved_ver(name) = @resolved_versions&.[](name)

  def dep_closure(name)
    return DepResolver.dep_closure(name, build_dep_graph)
  end

  # Given an array of [name, ver] pairs requested by the user, compute
  # the full install plan: transitive deps resolved, already-installed
  # packages filtered out, topological order (deps first).
  #
  # Returns: Array of [name, ver] pairs in install order. The `ver`
  # for auto-resolved deps is nil (meaning default_ver during install).
  def resolve_install_plan(requested_pairs)
    graph = build_dep_graph
    user_vers = requested_pairs.to_h

    # Resolve every version in the closure first: a dependency pinned
    # by one of the requested packages must count as installed (or not)
    # at the version it is pinned to, not at its default. All the
    # requested packages go in together, so pins that disagree across
    # them are caught instead of quietly resolved by merge order.
    versions = resolved_versions_for(requested_pairs)

    # Remember them for the builds that follow. A package being built
    # needs to know which version of a DEPENDENCY it is being built
    # against, and cannot work it out for itself: mpfr's own dep list
    # names host_gmp with no version, so resolving from mpfr alone
    # yields gmp's default -- while the gcc that asked for all of this
    # pinned 6.3.0. Resolving from mpfr gave 6.2.1 and the build
    # stopped on a gmp that was never installed.
    @resolved_versions = versions

    # Build the set of already-installed package names.
    installed = Set.new
    @packages.each_value do |pkg|
      ver = user_vers[pkg.name] || versions[pkg.name] || pkg.default_ver
      installed.add(pkg.name) if ver && pkg.installed?(ver)
    end

    requested_names = requested_pairs.map(&:first)
    ordered_names = DepResolver.resolve(requested_names, graph, installed)

    # Map back to [name, ver] pairs. A version the user asked for is
    # passed through as-is. Otherwise a version is passed only when a
    # pin moved it off the default — leaving it nil is what tells
    # install() this is a default install rather than a pinned one.
    ordered_names.map { |name|
      next [name, user_vers[name]] if user_vers[name]
      pkg = get(name)
      v = versions[name]
      [name, (v && pkg && v != pkg.default_ver) ? v : nil]
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
  NEVER_REMOVE = ["ruby"].freeze

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
  def clean(dry, except: [], force: false)
    return uninstall("ALL", dry, force, "ALL", "ALL", "ALL", except: except)
  end

  def uninstall(pkg_or_name, dry, force, ver = nil, compiler = nil,
                arch = nil, coords: nil, except: [])

    if pkg_or_name.blank?
      raise ArgumentError, "Invalid package name: '#{pkg_or_name}'"
    end

    all_pkgs  = (pkg_or_name.eql? "ALL")
    all_ver   = (ver.eql?         "ALL")
    all_cc    = (compiler.eql?    "ALL")
    all_arch  = (arch.eql?        "ALL")

    # Whether the CALLER named a version, which decides what "it is
    # not installed" means further down. Recorded before the default
    # is filled in, because afterwards the two are indistinguishable.
    asked_for_ver = !all_ver && !ver.blank?

    # Downgrade an empty string to nil (= default/auto)
    ver       = nil if ver.blank?
    compiler  = nil if compiler.blank?
    arch      = nil if arch.blank?

    pkg = !all_pkgs ? get_smart(pkg_or_name) : nil
    if pkg
      name          = pkg.name
      default_cc    = pkg.default_cc
      default_ver   = pkg.default_ver
      default_arch  = pkg.default_arch
      install_list  = pkg.get_install_list

      assert { default_cc.nil? == default_arch.nil? }
      assert { !default_ver.nil? }
    else
      name          = pkg_or_name
      default_arch  = ARCH
      default_cc    = ARCH.gcc_ver
      default_ver   = nil           # see below

      # For ALL: include both registered and orphan installations.
      # For an unrecognized single name: only orphans (best effort).
      install_list  = all_pkgs ? @known_installed + @found_installed
                               : @found_installed
      warning "Not recognized package name: #{name}" unless all_pkgs

      if !all_pkgs
        # A single unrecognized name matches only orphans, and there is
        # no package object to ask which compiler and arch they were
        # built for — that is what makes them orphans. The defaults
        # above describe the current target, which a host-side orphan
        # never matches, so without this the selection below silently
        # removes nothing. Unless the user narrowed it explicitly,
        # match every installation of that name. ALL keeps its own
        # semantics: default compiler and arch unless asked otherwise.
        all_cc    = true if compiler.nil?
        all_arch  = true if arch.nil?
      end
    end

    # A host package's compiler is whatever the package says it is,
    # and never the cross compiler for ARCH -- which is what the
    # fall-through below would otherwise pick, since arch defaults to
    # i386 when nobody named one.
    #
    # The test used to be `default_cc == "syscc"`, which was the same
    # question while every host package answered "syscc". A :stack
    # package now answers with the GCC whose stack it lives in, so the
    # proxy stopped being true and `-u host_qemu:6.2.0` quietly
    # matched nothing: it was looking for an install built by the
    # i386 cross compiler. on_host is what was being asked all along.
    if pkg&.on_host && (!arch || all_arch)
      compiler  ||= default_cc
    end

    # Set arch and ver to their defaults for this package, if they're unset.
    arch      ||= default_arch
    ver       ||= default_ver

    if default_arch
      # If the compiler is still unset, now pick up the gcc_ver for the given
      # arch, even if that is the result of a default value, not manually set.
      compiler  ||= ALL_ARCHS[ all_arch ? ARCH : arch ].gcc_ver
    end

    if ver.nil?
      # The version can still be `nil` here if a package name was provided,
      # and we didn't recognize the package. In this case, the default_ver
      # is `nil` and if `ver` is nil as well, we end up here.
      assert { pkg.nil? }
      all_ver = true
    elsif !install_list.any? { |e| e.ver == ver }

      if asked_for_ver
        #
        # The user named a version and it is not installed. There is
        # nothing to do, and doing "whatever IS installed" instead is
        # catastrophic: `-u host_gcc:11.5.0` on a tree holding six GCC
        # majors would take all six. It did.
        #
        warning "#{name} #{ver} is not installed: nothing to uninstall"
        return 0
      end

      # No version was named and the DEFAULT is not installed, which
      # is the case this fallback is for: uninstall what is actually
      # there rather than nothing.
      all_ver = true
    end

    # An arch is not a coordinate; it is two thirds of one. riscv64
    # builds for qemu-virt AND licheerv-nano, in separate trees, and
    # `e.arch == arch` matches both -- so `-u zlib` on riscv64 removed
    # the licheerv-nano copy along with the one the user was looking
    # at, and nothing said so. force_remove was fixed for exactly this
    # and plain -u was not.
    #
    # The board only narrows when the caller did not ask for every
    # arch and did not name the coordinates itself: -a ALL and --clean
    # mean every board, and force_remove computes its own set.
    # Target packages only: a board is a coordinate of the Tilck side.
    # A host install's env names a distro or "any", and a noarch one
    # has no board at all, so narrowing either by one excludes it.
    narrow_by_board = coords.nil? && !all_arch && pkg &&
                      !pkg.on_host && !pkg.arch_list.nil?

    board = narrow_by_board ? pkg.target_board(arch) : nil

    to_remove = install_list.select { |e|
      (all_pkgs   || e.pkgname == name     ) &&
      (all_ver    || e.ver == ver          ) &&
      (all_arch   || e.arch == arch        ) &&
      (all_cc     || e.compiler == compiler) &&
      (board.nil? || e.coords.nil? || e.coords.env == board) &&
      (coords.nil? || coords.include?(e.coords)) &&
      !except.include?(e.pkgname) &&
      !NEVER_REMOVE.include?(e.pkgname)
    }

    if all_pkgs && !force
      # When the package name is ALL, exclude the cross compilers
      # unless `force` is also true.
      #
      # Both ways of being one count. InstallInfo#compiler? reads the
      # target_arch metadata, which only the GCC package attaches; a
      # package that merely DECLARES is_compiler would otherwise be
      # swept up by a plain -u ALL, which is the one thing the
      # no-force form exists to prevent.
      to_remove = to_remove.select { |e|
        !(e.compiler? || e.pkg&.is_compiler)
      }
    end

    # Nothing matched, and the caller named something specific.
    #
    # An uninstall that removes nothing and says nothing is the most
    # expensive output this tool has produced: `-u host_qemu:6.2.0`
    # exited 0 with no output while the package sat there, and a
    # forced rebuild announced a removal and then reported "already
    # installed" -- twice, in one session, for two different reasons.
    # Silence reads as success.
    #
    # ALL is exempt: `-u ALL` on a clean tree is a no-op by design,
    # and so is --clean.
    if to_remove.empty? && !all_pkgs
      warning "#{name}: nothing matched, so nothing was removed"

      # What DOES exist under that name, since the usual cause is
      # asking about one set of coordinates while it lives at
      # another -- another arch, board, stack or version.
      elsewhere = install_list.select { |e|
        e.pkgname == name && !e.path.nil?
      }

      for e in elsewhere.first(8) do
        warning "  it is installed at #{e.coords}, version #{e.ver}"
      end

      return 0
    end

    p = "[DRY RUN] " if dry
    removed = 0

    for info in to_remove do
      puts "#{p}Remove pkg '#{info.pkgname}' install at #{info.path}"
      if !dry
        FileUtils.rm_rf(info.path)

        # Clean up empty parent directories left behind (pkg dir,
        # arch dir) so stale empty trees don't confuse the listing.
        parent = info.path.parent
        while parent != TC && parent.directory? &&
              Dir.empty?(parent)
          FileUtils.rmdir(parent)
          parent = parent.parent
        end

        removed += 1
      end
    end

    # The sysroot is a view over what is installed, so removing
    # something invalidates it exactly as installing something does.
    # Without this it keeps symlinks pointing at packages that are no
    # longer there, and the next thing to build against it fails in a
    # way that looks nothing like the cause.
    if removed > 0 && !host_stacks.empty?
      refresh()
      # Every stack, not just the default: an uninstall can invalidate
      # any of them, and a stale symlink is the failure mode hardest to
      # notice.
      host_stacks.each { |v| compose_stack_sysroot(Ver(v)) }
    end

    # How many were taken -- or, in a dry run, how many would be.
    # A caller that reports "removed nothing" when it selected fifty
    # is worse than one that says nothing at all.
    return dry ? to_remove.length : removed
  end

  private

  # Walk <root>/<pkg>/<ver>/ and emit an InstallInfo per (pkg, ver) whose
  # path is NOT already claimed by a registered package. Used by
  # scan_toolchain() to discover orphan installations.
  # A directory level naming a compiler rather than a package.
  #
  # The prefix alone is NOT enough, and portable/ is where that bites:
  # the musl cross-compilers are packages called gcc-i386-musl,
  # gcc-x86_64-musl and gcc-riscv64-musl. Reading those as compiler
  # slots descends one level too far and takes their bin/, share/ and
  # include/ directories for version numbers.
  #
  # A slot is the prefix followed by a VERSION -- gcc-14.4.0,
  # clang-14.0.0 -- which no package name is, since a package's
  # version lives in the directory below it rather than in its name.
  # Scan one <pkg>/ directory, whose children are version directories.
  def scan_one_pkg_dir(pkg_path, pkg_name, arch_obj, on_host, coords, list)

    return if !pkg_path.directory?

    for ver_str in Dir.children(pkg_path)
      full_path = pkg_path / ver_str
      next if @known_pkgs_paths&.include?(full_path)
      ver = SafeVer(ver_str)

      if ver.nil?
        warning "Invalid package version: #{full_path}"
        next
      end

      list << InstallInfo.new(
        pkg_name, "syscc", on_host, arch_obj, ver, full_path,
        coords: coords
      )
    end
  end

  #
  # Walk the whole toolchain: <machine>/<env>/<stack>/pkgs/<pkg>/<ver>/
  #
  # One loop for everything, because every install is at the same
  # depth with the same meaning per level. toolchain4 needed four
  # different walks and a predicate to tell a compiler directory from
  # a package with a similar name; there is nothing left to
  # disambiguate, since a package can only appear under pkgs/.
  #
  def scan_toolchain

    list = []
    return list if !TC.directory?

    for machine in Dir.children(TC).sort
      next if NON_INSTALL_DIRS.include?(machine)

      m_dir = TC / machine
      next if !m_dir.directory?

      arch_obj, on_host, known = machine_to_arch(machine)
      next if !known

      for env in Dir.children(m_dir).sort
        e_dir = m_dir / env
        next if !e_dir.directory?

        for stack in Dir.children(e_dir).sort
          pkgs = e_dir / stack / "pkgs"
          next if !pkgs.directory?

          c = Coords.new(machine, env, stack)

          for pkg_name in Dir.children(pkgs).sort
            scan_one_pkg_dir(
              pkgs / pkg_name, pkg_name, arch_obj, on_host, c, list
            )
          end
        end
      end
    end

    return list
  end

  # Top-level directories that are not machines.
  NON_INSTALL_DIRS = ["cache", "staging"].freeze

  # Turn a <machine> coordinate back into [Architecture, on_host].
  #
  # "noarch" has neither; a Tilck target names its arch directly; and
  # anything else is a build machine, whose packages run on the host.
  # Returns [Architecture, on_host, known?]. A machine we cannot
  # identify is skipped rather than scanned: reading its packages
  # would attribute them to a nil architecture and let them show up
  # in listings for a target that does not exist.
  def machine_to_arch(machine)

    return [nil, false, true] if machine == "noarch"

    if machine.start_with?("tilck-")
      name = machine.delete_prefix("tilck-")
      arch = ALL_ARCHS[name]
      warning "Unknown architecture '#{name}' in #{TC / machine}" if !arch
      return [arch, false, !arch.nil?]
    end

    # Any other machine is a build host. Only this one's packages can
    # run here, so anything else is another machine's business.
    return [HOST_ARCH, true, machine == HOST_OS_ARCH]
  end

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
