# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'package'
require_relative 'dep_resolver'
require_relative 'version_solver'
require_relative 'sysroot'
require_relative 'hermeticity'

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
    @target_arch = nil    # nil = fall back to the global ARCH
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
    arch_dir = TC / "gcc-#{arch.gcc_ver}" / arch.name
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
    banner = ->(s) { puts; puts "--- #{s.center(40)} ---" }

    list_with_paths = @known_installed + @found_installed
    by_path = {}

    for info in list_with_paths
      p = info.path
      if (!by_path.include? p) or by_path[p].pkg.nil?
        by_path[p] = info
      end
    end

    list = by_path.values() + @installable

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

      *list.select { |x| Version === x.compiler }.
        map { |x| x.compiler }.uniq.select { |cc| cc == curr_cc }.
          map { |cc|
            [
              "Packages built by GCC #{cc} [ CURRENT ]",
              list.select { |x| x.compiler == cc }
            ]
          },

      *list.select { |x| Version === x.compiler and all_compilers }.
        map { |x| x.compiler }.uniq.select { |cc| cc != curr_cc }.
          map { |cc|
            [
              "Packages built by GCC #{cc}",
              list.select { |x| x.compiler == cc }
            ]
          }
    ]

    #list.each { |x| puts x }  # DEBUG

    for msg, l in groups do
      banner.call msg
      l.map { |x| x.pkgname }.uniq.each { |pkg|
        show_status(pkg, group_by, l.select { |x| x.pkgname == pkg })
      }
    end

    puts
  end

  def show_status(name, group_by, list)

    add_braces = ->(s) { "{#{s}}" }

    if list.nil? or list.empty?
      puts "#{name.ljust(35)} [ #{Package::EMPTY_STR} ]"
      return
    end

    if list.all? { |e| e.compiler.eql? "syscc" }
      atos = ->(a) { get_human_arch_name(a) }
    else
      atos = ->(a) { a.nil?? "noarch" : a.name }
    end

    # Split into working installs and broken ones. Only working
    # installs count for the arch/ver display and "installed" status.
    installed = list.filter { |e| !e.path.nil? && !e.broken }
    broken = list.filter { |e| !e.path.nil? && e.broken }

    archs = installed.map{ |e| atos.call(e.arch) }.uniq
    vers = installed.map { |e| e.ver }.uniq

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
            }.map(&:ver).uniq.map(&:to_s).join(", ")
          )
        ].join(": ")
      }.join(", ")

    elsif group_by == 'ver'

      s = vers.map {
        |v|
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
        status = Package::INSTALLED_STR
      elsif !broken.empty?
        status = Package::BROKEN_STR
      else
        status = Package::EMPTY_STR
      end
    else
      if list.any? { |x| !x.path.nil? }
        status = Package::FOUND_STR
      else
        status = Package::EMPTY_STR
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
      error "The hermetic toolchain is opt-in: set TILCK_HERMETIC=1"
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
      InstallOrigin.write(inst.path, default_install) if inst

      # The sysroot is a view over what is installed, so it is stale
      # the moment that changes.
      # Recompose whenever the package contributes to the sysroot, which
      # is not the same as being a hermetic package: host_gcc is
      # :distro and still contributes its target runtime.
      if !pkg.sysroot_fragments.empty?
        compose_hermetic_sysroot

        # Audit after composing, not before: a package whose paths name
        # the sysroot cannot be inspected until the sysroot is real.
        # Only hermetic packages are audited — binutils and gcc link
        # the system libc by design.
        ok = audit_hermetic_install(pkg, ver) if pkg.host_tier == :hermetic

        # Now that the sysroot includes this package, let it check
        # whatever it could not check before.
        ok = false if !pkg.post_sysroot_check
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

  # The hermetic stack's root, and the sysroot inside it. Defined here
  # rather than on Package because several packages, the sysroot
  # composition and the audit all need the same answer.
  def hermetic_root

    ver = get_config_ver("gcc", host: true)

    if ver.nil?
      raise "HOST_VER_GCC is missing from other/host_pkg_versions: it " \
            "names the hermetic stack's directory, so without it every " \
            "hermetic package would install to the same broken path"
    end

    return HOST_DIR_HERMETIC_BASE / "gcc-#{ver}"
  end

  def hermetic_sysroot = hermetic_root / "sysroot"

  # readelf and the dynamic loader the audit uses. Ours when we have
  # them — we build binutils, so readelf is a tool we own — falling
  # back to the system readelf, which reads the same ELF either way.
  # Without our loader there is no resolution check, and the audit
  # reports that rather than passing silently.
  def audit_tools

    bu = get("host_binutils")
    bu_inst = bu&.find_install(bu.default_ver)
    readelf = bu_inst ? bu_inst.path / "install/bin/readelf" : "readelf"

    libc = get("host_glibc")
    libc_inst = libc&.find_install(libc.default_ver)
    loader = libc_inst ?
      libc_inst.path / "install/usr/lib/ld-linux-x86-64.so.2" : nil

    return [readelf, loader]
  end

  # Check that an installed hermetic package references nothing outside
  # the toolchain. Returns true when clean.
  #
  # Only hermetic packages are audited. binutils and gcc are :distro
  # and link the system libc by design: they are build tools, and what
  # they link against never reaches what they produce.
  def audit_hermetic_install(pkg, ver)

    inst = pkg.find_install(ver)
    return true if inst.nil?

    readelf, loader = audit_tools
    if loader.nil?
      warning "No hermetic loader yet: auditing #{pkg.name} without " \
              "checking where its libraries resolve"
    end

    hostile = pkg.hermeticity_hostile_check?
    if !hostile
      info "#{pkg.name}: auditing without a hostile LD_LIBRARY_PATH " \
           "(see Package#hermeticity_hostile_check?)"
    end

    violations = Hermeticity.audit(
      inst.path, allowed: [TC], readelf: readelf, loader: loader,
      hostile: hostile
    )

    return true if violations.empty?

    error "#{pkg.name} #{ver} is not hermetic:"
    for v in violations.first(20)
      error "  #{v}"
    end
    if violations.length > 20
      error "  ... and #{violations.length - 20} more"
    end

    return false
  end

  # Rebuild the sysroot from every installed hermetic package, each at
  # the version currently selected for it.
  #
  # Run after any hermetic install, because a package that bakes an
  # absolute --prefix naming the sysroot is broken until the sysroot
  # makes that path real: glibc's own libc.so.6 will not exec before
  # this has run, its ELF interpreter pointing into a directory that
  # does not exist yet.
  def compose_hermetic_sysroot

    fragments = @packages.values.flat_map { |p| p.sysroot_fragments }

    n = Sysroot.compose(hermetic_sysroot, fragments)
    info "Hermetic sysroot: #{n} entries from #{fragments.length} packages"
    return n
  end

  # Transitive dependency closure of `name`, nearest dependency first.
  # Used by Package#deps_build_env to collect the build interfaces a
  # package's dependencies publish.
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
  def uninstall(pkg_or_name, dry, force, ver = nil, compiler = nil, arch = nil)

    if pkg_or_name.blank?
      raise ArgumentError, "Invalid package name: '#{pkg_or_name}'"
    end

    all_pkgs  = (pkg_or_name.eql? "ALL")
    all_ver   = (ver.eql?         "ALL")
    all_cc    = (compiler.eql?    "ALL")
    all_arch  = (arch.eql?        "ALL")

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

    # Check if the default compiler for this package is "syscc", meaning this
    # is very likely a host tool, like a cross-compiler.
    syscc     = (default_cc.eql? "syscc")

    # If compiler is unset, check if the default is syscc. If that's the case,
    # check if arch is nil or ALL, and if that's the case, use the default cc,
    # ignoring the gcc_ver for the given ARCH. That's important as we could
    # have arch=nil ===> ARCH=i386 by default and then pick up the *default*
    # gcc_ver for that (default) arch and that would make *no* sense: if
    # `arch` is not explicitly set to a specific value and the default cc is
    # "syscc", we set it.
    if syscc && (!arch || all_arch)
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
      # The configured default version is not installed; fall back to
      # uninstalling whatever version IS installed.
      all_ver = true
    end

    to_remove = install_list.select { |e|
      (all_pkgs   || e.pkgname == name     ) &&
      (all_ver    || e.ver == ver          ) &&
      (all_arch   || e.arch == arch        ) &&
      (all_cc     || e.compiler == compiler)
    }

    if all_pkgs && !force
      # When the package name is ALL, we need to exclude all the cross compilers
      # from the list, unless `force` is also true.
      to_remove = to_remove.select { |e| !e.compiler? }
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
    if removed > 0 && hermetic_sysroot.directory?
      refresh()
      compose_hermetic_sysroot
    end
  end

  private

  # Walk <root>/<pkg>/<ver>/ and emit an InstallInfo per (pkg, ver) whose
  # path is NOT already claimed by a registered package. Used by
  # scan_toolchain() to discover orphan installations.
  def scan_pkg_dir_tree(root, compiler, on_host, arch_obj, list)
    return if !root.directory?
    for pkg_name in Dir.children(root)
      pkg_path = root / pkg_name
      next if !pkg_path.directory?
      for ver_str in Dir.children(pkg_path)
        full_path = pkg_path / ver_str
        next if @known_pkgs_paths.include? full_path
        ver = SafeVer(ver_str)
        if ver.nil?
          warning "Invalid package version: #{full_path}"
          next
        end
        list << InstallInfo.new(
          pkg_name, compiler, on_host, arch_obj, ver, full_path
        )
      end
    end
  end

  def scan_toolchain

    list = []

    # Target-side: TC/gcc-<ver>/<arch>/<pkg>/<ver>/
    for cc_dir in Dir.children(TC)
      next if !cc_dir.start_with?("gcc-")

      cc_ver = SafeVer(cc_dir.sub("gcc-", ""))
      if cc_ver.nil?
        warning "Invalid compiler directory: #{TC / cc_dir}"
        next
      end

      for arch_name in Dir.children(TC / cc_dir)
        arch_obj = ALL_ARCHS[arch_name]
        if !arch_obj
          warning "Unknown architecture '#{arch_name}' in #{TC / cc_dir}"
          next
        end
        scan_pkg_dir_tree(TC / cc_dir / arch_name, cc_ver, false, arch_obj, list)
      end
    end

    # Noarch: TC/noarch/<pkg>/<ver>/
    scan_pkg_dir_tree(TC / "noarch", nil, false, nil, list)

    # Host-side: TC/host/<os>-<arch>/{portable,<distro>/<host-cc>}/<pkg>/<ver>/
    host_root = TC / "host"
    if host_root.directory?
      for os_arch in Dir.children(host_root)
        os_dir = host_root / os_arch
        next if !os_dir.directory?

        # Tier 1: portable (shared across distros and host compilers).
        scan_pkg_dir_tree(os_dir / "portable", "syscc", true, HOST_ARCH, list)

        # Tier 4: hermetic/<compiler>-<ver>/pkgs/<pkg>/<ver>/. Scanned
        # explicitly rather than falling through the distro loop
        # below, whose <distro>/<host-cc>/ shape it does not share:
        # read that way, "pkgs" looks like a package name and the
        # package names look like versions. The sysroot beside pkgs/
        # is a composed view, not an installation, and is not scanned
        # at all.
        hermetic_dir = os_dir / "hermetic"
        if hermetic_dir.directory?
          for stack in Dir.children(hermetic_dir)
            scan_pkg_dir_tree(
              hermetic_dir / stack / "pkgs", "syscc", true, HOST_ARCH, list
            )
          end
        end

        for sub in Dir.children(os_dir)
          next if sub == "portable"
          next if sub == "hermetic"
          distro_dir = os_dir / sub
          next if !distro_dir.directory?

          # Tier 2: distro-bound, compiler-independent packages live
          # directly under <distro>/<pkg>/<ver>/. Scan each child that
          # isn't a host-cc slot or the ruby bootstrap.
          for pkg_name in Dir.children(distro_dir)
            next if pkg_name.start_with?("gcc-") ||
                    pkg_name.start_with?("clang-")
            next if pkg_name == "ruby"
            pkg_path = distro_dir / pkg_name
            next if !pkg_path.directory?
            for ver_str in Dir.children(pkg_path)
              full_path = pkg_path / ver_str
              next if @known_pkgs_paths.include? full_path
              ver = SafeVer(ver_str)
              if ver.nil?
                warning "Invalid package version: #{full_path}"
                next
              end
              list << InstallInfo.new(
                pkg_name, "syscc", true, HOST_ARCH, ver, full_path
              )
            end
          end

          # Tier 3: compiler-bound packages under <distro>/<host-cc>/.
          for host_cc in Dir.children(distro_dir)
            next if !(host_cc.start_with?("gcc-") ||
                      host_cc.start_with?("clang-"))
            scan_pkg_dir_tree(
              distro_dir / host_cc, "syscc", true, HOST_ARCH, list
            )
          end
        end
      end
    end

    return list
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
