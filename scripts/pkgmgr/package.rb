# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'package_manager'

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

  attr_reader :pkgname, :compiler, :on_host, :arch, :ver, :path
  attr_reader :pkg, :target_arch, :libc

  def initialize(
    pkgname,  # package name (string)
    compiler, # compiler version used to build it, or "syscc" or nil for noarch
    on_host,  # runs on the host?
    arch,     # arch. of the installation (e.g. HOST_ARCH for compilers)
    ver,      # package version (Version object)
    path,     # installation path (directory)
    pkg = nil,# Package object or nil.
    target_arch = nil, # target architecture [only for compilers]
    libc        = nil  # libc (e.g. "musl") [only for compilers]
  )
    @pkgname = pkgname         # package name
    @compiler = compiler       # "syscc" or compiler version or nil (= noarch)
    @on_host = on_host         # runs on host_$arch or on $arch (=Tilck) ?
    @arch = arch               # arch object or nil (= noarch)
    @ver = ver                 # package version
    @path = path               # install path
    @pkg = pkg                 # package object
    @target_arch = target_arch
    @libc = libc
    assert { arch.nil? or arch.is_a? Architecture }
    freeze
  end

  def compiler? = !@target_arch.nil?

  def get_human_arch_name
    return "noarch" if arch.nil?
    return "host" if arch == HOST_ARCH
    return arch.name
  end

  def to_s = ("I{ " +
      "pkg: #{@pkgname.ljust(20)}, comp: #{@compiler.to_s.ljust(6)}, " +
      "arch: #{((@on_host?'host_':'')+@arch.to_s).ljust(11)}, " +
      "ver: #{@ver.to_s.ljust(25)}, target: #{@target_arch.to_s.ljust(7)}, " +
      "libc: #{@libc.to_s.ljust(5)}, " +
      "path: #{@path.sub(TC.to_s + '/', '')}" +
  " }")

end

class Package

  attr_reader :name, :on_host, :is_compiler, :arch_list, :dep_list
  attr_reader :install_list

  STATUS_LEN    = 9
  INSTALLED_STR = Term.makeGreen("installed".center(STATUS_LEN))
  FOUND_STR     = Term.makeBlue("found".center(STATUS_LEN))
  SKIPPED_STR   = Term.makeYellow("skipped".center(STATUS_LEN))
  BROKEN_STR    = Term.makeRed("broken".center(STATUS_LEN))
  EMPTY_STR     = "".center(STATUS_LEN)

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

    assert {
      !!on_host == !!(name.start_with? "host_" or name.start_with? "gcc_")
    }
  end

  def id = @name
  def ==(other) = (other.is_a?(Package) ? id == other.id : super)
  def eql?(other) = (self == other)
  def hash = (id.hash)

  def refresh
    @install_list = get_install_list()
  end

  # Methods not implemented in the base class
  def install_impl(ver = nil) = raise NotImplementedError
  def get_install_list = raise NotImplementedError
  def default_arch = ARCH
  def default_cc = ARCH.gcc_ver
  def default_ver = pkgmgr.get_config_ver(@name)
end


