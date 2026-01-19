# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'package'

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
    return get("gcc_#{arch}_musl")
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
    arch_dir = TC / arch.gcc_ver._ / arch.name
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

  def build_gcc_package_name(target_arch, libc)
    return "gcc_#{target_arch.name}_#{libc}"
  end

  def parse_gcc_dir(s)
    if s =~ /\Agcc_(\d+)_(\d+)_(\d+)_(\w+)_musl\z/
      arch = ALL_ARCHS[$4]
      if arch
        return [ Ver($1 + "." + $2 + "." + $3), arch, "musl" ]
      end
    end
    return nil
  end

  def show_status_all(group_by = nil, all_compilers = false)

    curr_cc = ARCH.gcc_ver
    banner = ->(s) { puts; puts "--- #{s.center(40)} ---" }
    show_curr_compiler = ->(cc) {
      cc == curr_cc ? " [ CURRENT ]" : ""
    }

    list = @known_installed + @found_installed + @installable
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

    # Get an unique list of archs from all the installations
    archs = list.map{ |e| atos.call(e.arch) }.uniq
    vers = list.map { |e| e.ver }.uniq
    list = list.filter { |e| !e.path.nil? }

    if group_by.nil?

      s = archs.join(", ")

    elsif group_by == 'arch'

      s = archs.map {
        |a|
        [
          a,
          add_braces.call(
            list.filter {
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
            list.filter {
              |e| e.ver == v
            }.map(&:arch).uniq.map(&atos).join(", ")
          )
        ].join(": ")
      }.join(", ")

    end

    if list.any? { |x| !x.pkg.nil? }
      if list.any? { |x| !x.path.nil? }
        if list.any? { |x| x.broken }
          status = Package::BROKEN_STR
        else
          status = Package::INSTALLED_STR
        end
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

    pkg = get_smart(pkg)
    if !pkg
      error "Package not found: #{pkg_or_name}"
      return false
    end

    ver = nil if ver.blank?
    ver ||= pkg.default_ver()
    pkg.install_impl(ver)
    return true
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
      install_list  = @found_installed
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

      # The only reasonable thing to do here is to set all_ver = true.
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
      end
    end
  end

  private
  def scan_toolchain

    list = []

    handle_package = ->(cc, arch, name) {
      path = TC / (cc || "") / (arch || "noarch") / name
      return if @known_pkgs_paths.include? path

      on_host = (arch&.start_with? "host_") || false
      parsed_gcc_info = parse_gcc_dir(name)
      cc = ((cc.eql? "syscc") ? "syscc" : Ver(cc)&.to_dot)
      arch_obj = (arch && ALL_ARCHS[arch.sub("host_", "")]) || nil

      if !parsed_gcc_info
        for ver_str in Dir.children(path)
          full_path = path / ver_str
          ver = SafeVer(ver_str)
          if !ver
            warning "Invalid package version: #{path / ver_str}"
            next
          end
          list << InstallInfo.new(name, cc, on_host, arch_obj, ver, full_path)
        end
      else
        # GCC toolchains embed their version into the directory name e.g.:
        #
        #   gcc_13_3_0_i386_musl
        #
        # While to be consistent, toolchains should have had the same package
        # name as here (e.g. gcc_i386_musl) and a subdirectory with their
        # version.
        ver, target_arch, libc = parsed_gcc_info
        name = build_gcc_package_name(target_arch, libc)
        list << InstallInfo.new(
          name,
          "syscc",
          true,
          arch_obj,
          ver,
          path,
          nil,
          false,
          target_arch,
          libc
        )
      end
    }

    for cc in Dir.children(TC)
      next if cc == ".distro"
      next if ["cache", "noarch"].include? cc

      if cc != "syscc"
        cc_ver = SafeVer(cc)
        if cc_ver.nil? or cc_ver.type != VersionType::UNDERSCORE
          warning "Invalid directory: #{TC / cc}"
          next
        end
      end

      for arch in Dir.children(TC / cc)
        arch_obj = ALL_ARCHS[arch.sub("host_", "")]
        on_host = arch.start_with? "host_"

        if !arch_obj
          warning "Unknown architecture '#{arch}' in #{TC / cc}"
          next
        end

        for name in Dir.children(TC / cc / arch)
          assert { !cc.nil? and !arch.nil? and !name.nil? }
          handle_package.call(cc, arch, name)
        end
      end
    end

    for name in Dir.children(TC / "noarch")
      handle_package.call(nil, nil, name)
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
