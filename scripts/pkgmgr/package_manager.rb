# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'package'
require_relative 'dep_resolver'

require 'singleton'
require 'set'

class PackageManager

  include Singleton
  attr_reader :packages

  def initialize
    @packages = {}
    @config_versions = read_config_versions()
    @known_pkgs_paths = nil
    @known_installed = nil
    @found_installed = nil
    @installable = nil
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

  def get_tc(arch)
    return get("gcc-#{arch}-musl")
  end

  def get_config_ver(name)
    return @config_versions[name._.upcase.sub("HOST_", "")]
  end

  def get_smart(pkg_or_name)
    assert { pkg_or_name.is_a? Package or pkg_or_name.is_a? String }
    (pkg_or_name.is_a? Package) ? pkg_or_name : get(pkg_or_name)
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
    show_curr_compiler = ->(cc) {
      cc == curr_cc ? " [ CURRENT ]" : ""
    }

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

    # Enforce arch_list for regular target packages. Host packages and noarch
    # packages (arch_list == nil) are exempt. We check pkg.default_arch (not
    # ARCH directly) so the filter stays consistent with the InstallInfo
    # produced by regular_target_package_get_installable_list — both use
    # default_arch as the source of truth for "the arch this package builds
    # for in the current invocation context".
    if !pkg.on_host && !pkg.arch_list.nil?
      a = pkg.default_arch
      if a.nil? || !pkg.arch_list.include?(a.name)
        a_name = a.nil? ? "<nil>" : a.name
        error "Package #{pkg.name} is not supported for arch #{a_name}"
        return false
      end
    end

    ver = nil if ver.blank?
    ver ||= pkg.default_ver()
    ok = pkg.install_impl(ver)
    if ok
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
  # the cross-compiler for the current ARCH, since Package#install_impl
  # calls with_cc() which requires the compiler to be installed.
  def build_dep_graph
    cc_name = "gcc-#{ARCH.name}-musl"
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

  # Given an array of [name, ver] pairs requested by the user, compute
  # the full install plan: transitive deps resolved, already-installed
  # packages filtered out, topological order (deps first).
  #
  # Returns: Array of [name, ver] pairs in install order. The `ver`
  # for auto-resolved deps is nil (meaning default_ver during install).
  def resolve_install_plan(requested_pairs)
    graph = build_dep_graph

    # Build the set of already-installed package names.
    installed = Set.new
    @packages.each_value do |pkg|
      user_ver = requested_pairs.find { |n, _| n == pkg.name }&.last
      ver = user_ver || pkg.default_ver
      installed.add(pkg.name) if ver && pkg.installed?(ver)
    end

    requested_names = requested_pairs.map(&:first)
    ordered_names = DepResolver.resolve(requested_names, graph, installed)

    # Map back to [name, ver] pairs. User-specified versions are
    # preserved; auto-resolved deps get nil (install() will use
    # default_ver).
    user_vers = requested_pairs.to_h
    ordered_names.map { |name| [name, user_vers[name]] }
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
      end
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

        # Portable half (shared across distros and host compilers).
        scan_pkg_dir_tree(os_dir / "portable", "syscc", true, HOST_ARCH, list)

        # Distro-bound half: <distro>/<host-cc>/<pkg>/<ver>/
        #
        # A dir under <distro>/ counts as a host-cc dir only if it looks like
        # "<family>-<version>" (e.g. gcc-11.4.0, clang-14.0.0). Anything else
        # is a bootstrap tool living in the distro slot (e.g. the Ruby used
        # to run the package manager itself, at <distro>/ruby/<ver>/) — skip
        # it silently since it's not a registered package.
        for sub in Dir.children(os_dir)
          next if sub == "portable"
          distro_dir = os_dir / sub
          next if !distro_dir.directory?
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

  def read_config_versions
    result = {}
    data = File.read(MAIN_DIR / "other" / "pkg_versions")

    for line in data.split("\n")
      if !line.start_with? "VER_"
        raise "Invalid line in pkg_versions: #{line}"
      end

      line = line.sub("VER_", "")
      key, value = line.split("=")

      if key.blank? || value.blank?
        raise "Invalid line in pkg_versions: #{line}"
      end

      if result[key]
        raise "Duplicate key in pkg_versions: #{key}"
      end

      result[key] = Ver(value)
    end

    return result
  end

end # Class PackageManager

def pkgmgr = PackageManager.instance
