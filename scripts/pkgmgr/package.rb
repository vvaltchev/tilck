# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'

require 'singleton'

class PackageManager

  include Singleton

  def initialize
    @packages = {}
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

  def show_status_all
    for id, p in @packages do
      p.show_status
    end
  end
end

PackageDep = Struct.new(

  "PackageDep",

  :name,        # package name (string)
  :host,        # bool: runs on host or on the target?
  :ver          # Version: can be nil, meaning "latest"
)

def Dep(name, host, ver = nil)
  return PackageDep.new(name, host, ver)
end

class InstallInfo

  attr_reader :compiler, :on_host, :arch, :ver

  def initialize(compiler, on_host, arch, ver)
    @compiler = compiler       # nil meaining "syscc" or compiler version
    @on_host = on_host         # runs on host_$arch or on $arch ?
    @arch = arch               # arch object
    @ver = ver                 # package version
    assert { !arch.nil? }
    assert { on_host || !compiler }
    freeze
  end

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
    @install_list = get_install_list
  end

  def id = [@name, @is_host]

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

  # Not implemented methods
  def install(ver = nil) = raise NotImplementedError
  def get_install_list = raise NotImplementedError

end


