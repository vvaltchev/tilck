# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'

PackageDep = Struct.new(

  "PackageDep",

  :name,        # package name (string)
  :host,        # bool: runs on host or on the target?
  :ver          # Version: can be nil, meaning "default"
)

def Dep(name, host, ver = nil)
  return PackageDep.new(name, host, ver)
end

class InstallInfo

  attr_reader :compiler, :on_host, :arch, :ver, :path

  def initialize(pkgname, compiler, on_host, arch, ver, path)
    @pkgname = pkgname         # package name
    @compiler = compiler       # "syscc" or compiler version
    @on_host = on_host         # runs on host_$arch or on $arch (=Tilck) ?
    @arch = arch               # arch object
    @ver = ver                 # package version
    @path = path               # install path
    assert { !arch.nil? }
    assert { !on_host || compiler == "syscc" }
    freeze
  end

  def to_s = ("I{ " +
      "pkg:#{@pkgname.ljust(20)}, comp:#{@compiler}, " +
      "arch:#{@arch}, ver:#{@ver}, " +
      "path:#{@path.sub(TC.to_s, 'TCROOT')}" +
  " }")

end

class Package

  attr_reader :name, :on_host, :is_compiler, :arch_list, :dep_list

  INSTALLED_STR = Term.makeGreen("installed".center(11))
  SKIPPED_STR   = Term.makeYellow("skipped".center(11))
  BROKEN_STR    = Term.makeRed("broken".center(11))

  public
  def initialize(name:,
                 on_host: false,
                 is_compiler: false,
                 arch_list: ALL_ARCHS,
                 dep_list: [])
    @name = name
    @on_host = on_host
    @is_compiler = is_compiler
    @arch_list = arch_list
    @dep_list = dep_list
    @install_list = get_install_list()
  end

  def id = @name
  def ==(other)
    other.is_a?(Package) ? id == other.id : super
  end
  def eql?(other) = (self == other)
  def hash = (id.hash)

  def is_installed?(arch, compilerVer = nil, ver = nil)
    for info in @install_list do
      assert { info.on_host == @on_host }
      next if info.arch != arch
      next if ver && info.ver && ver != info.ver
      next if compilerVer && info.compiler && compilerVer != info.compiler
      return true
    end
    return false
  end

  def show_status

    def install_arch_str(info) =
      info.on_host ? "host" : info.arch.name

    def add_braces(s) = "{#{s}}"

    list = @install_list

    if list.empty?
      puts @name
      return
    end

    # Exclude installations for other host archs
    list.filter! { |x| x.arch == HOST_ARCH }

    # Get an unique list of archs from all the installations
    archs = list.map{|e| install_arch_str(e)}.uniq

    s = archs.map {
      |a|
      [
        a,
        add_braces(
          list.filter {
            |e| install_arch_str(e) == a
          }.map(&:ver).map(&:to_s).join(", ")
        )
      ].join(": ")
    }.join(", ")
    puts "#{@name.ljust(35)} [ #{INSTALLED_STR} ] [ #{s} ]"
  end

  # Install the package
  #
  # param `ver`:           version of the package to install
  # nil                 => default/auto/configured from ENV
  # other               => might or might not be supported, depending on the
  #                        package. Changes over time. It might not be possible
  #                        to install older versions of the package that were
  #                        supported before
  def install(ver = nil)
    ver = nil if ver.blank?
    install_impl(ver)
    @install_list = get_install_list()
  end

  # Delete the package
  #
  # param `ver`:           version of the package to delete
  # nil                 => default/auto/configured from ENV (like install())
  # '*'                 => delete all versions found (for the given compiler)
  # other               => delete a specific version, if exists.
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
  # param `arch`:          target architecture of the package to delete:
  #                        each package might have been built using multiple
  #                        compiler versions, for multiple target architectures
  #                        in multiple different versions.
  # nil                 => default/auto/configured from ENV (like install())
  # '*'                 => all architectures
  # other               => specific architecture (e.g. i386)
  def delete(ver = nil, compiler = nil, arch = nil)

    # Check if the default compiler for this package is "syscc", meaning this
    # is very likely a host tool, like a cross-compiler.
    syscc     = (default_cc.eql? "syscc")

    all_arch  = (arch.eql? "ALL")
    all_ver   = (ver.eql? "ALL")
    all_cc    = (compiler.eql? "ALL")

    # Downgrade an empty string to nil (= default/auto)
    arch      = nil if arch.blank?
    ver       = nil if ver.blank?
    compiler  = nil if compiler.blank?

    # If compiler is unset, check if the default is syscc. If that's the case,
    # check if arch is nil or ALL, and if that's the case, use the default cc,
    # ignoring the gcc_ver for the given ARCH. That's important as we could have
    # arch=nil => ARCH=i386 by default and then pick up the default gcc_ver for
    # that default arch and that would make no sense. If arch is not explicitly
    # set to a specific value and the default cc is "syscc", we set it.
    compiler  ||= (syscc && (!arch || all_arch)) && default_cc

    # Set arch and ver to their defaults for this package, if they're unset.
    arch      ||= default_arch
    ver       ||= default_ver

    # If the compiler is still unset, now pick up the gcc_ver for the given
    # arch, even if that is the result of a default value, not manually set.
    compiler  ||= ALL_ARCHS[arch].gcc_ver

    # Finally, compute the list of installed packages to remove.
    to_remove = @install_list.select { |e|
      (all_arch || e.arch == arch)         &&
      (all_ver  || e.ver == ver)           &&
      (all_cc   || e.compiler == compiler)
    }

    for pkg in to_remove do
      puts "Remove pkg #{@name} at #{pkg.path}"
      FileUtils.rm_rf(pkg.path)
      @install_list -= [pkg]
    end
  end

  # Methods not implemented in the base class
  protected

  def install_impl(ver = nil) = raise NotImplementedError
  def get_install_list = raise NotImplementedError
  def default_ver = raise NotImplementedError
  def default_arch = ARCH
  def default_cc = ARCH.gcc_ver
end


