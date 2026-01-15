# SPDX-License-Identifier: BSD-2-Clause

require_relative 'arch'

require 'power_assert'
require 'pathname'
require 'etc'

# Global, generic constants.
KB = 1024
MB = 1024 * KB

# Basic constants specific to this project.
DEFAULT_TC_NAME = "toolchain3"
OS = Etc.uname.fetch(:sysname)
RUBY_SOURCE_DIR = Pathname.new(File.realpath(__dir__))
MAIN_DIR = Pathname.new(RUBY_SOURCE_DIR.parent.parent)
GITHUB = "https://github.com"

# Generic utils
def getenv(name, default)
  val = ENV[name].to_s
  return !val.empty? ? val : default
end

def assert(&expr)
  PowerAssert.start(expr, assertion_method: __method__) do |ctx|
    ok = ctx.yield
    raise "Assertion failed:\n#{ctx.message}" unless ok
    true
  end
end

def mkpathname(path_str) = Pathname.new(path_str)

def make_gh_rel_download(user, proj, tag)
  return "#{GITHUB}/#{user}/#{proj}/releases/download/#{tag}"
end

module FileShortcuts
  module_function
  def exist?(...)      = File.exist?(...)
  def file?(...)       = File.file?(...)
  def symlink?(...)    = File.symlink?(...)
  def directory?(...)  = File.directory?(...)
  def readable?(...)   = File.readable?(...)
  def writable?(...)   = File.writable?(...)
  def executable?(...) = File.executable?(...)
  def basename(...)    = File.basename(...)
  def dirname(...)     = File.dirname(...)
  def extname(...)     = File.extname(...)
  def stat(...)        = File.stat(...)
  def lstat(...)       = File.lstat(...)
  def readlink(...)    = File.readlink(...)
  def expand_path(...) = File.expand_path(...)
  def realpath(...)    = File.realpath(...)
end

module FileUtilsShortcuts
  module_function
  def chdir(...)       = FileUtils.chdir(...)
  def getwd()          = FileUtils.getwd()
  def mkdir(...)       = FileUtils.mkdir(...)
  def mkdir_p(...)     = FileUtils.mkdir_p(...)
  def rm(...)          = FileUtils.rm(...)
  def rm_f(...)        = FileUtils.rm_f(...)
  def rm_r(...)        = FileUtils.rm_r(...)
  def rm_rf(...)       = FileUtils.rm_rf(...)
  def mv(...)          = FileUtils.mv(...)
  def symlink(...)     = FileUtils.symlink(...)
  def ln_s(...)        = FileUtils.ln_s(...)
  def ln_sf(...)       = FileUtils.ln_sf(...)
  def cp(...)          = FileUtils.cp(...)
  def cp_r(...)        = FileUtils.cp_r(...)
  def rmdir(...)       = FileUtils.rmdir(...)
end

# Monkey-patch String and NilClass to support blank? like in Rails.
class String
  def blank? = strip.empty?
  def _ = gsub("-", "_")  # Custom to this package manager
end

class NilClass
  def blank? = true
end

# Do the same for other built-in types, for convenience.
class TrueClass
  def blank? = false
end

class FalseClass
  def blank? = true
end

class Array
  def blank? = empty?
end

class Hash
  def blank? = empty?
end

class Object
  def blank? = false
end

module InitOnly

  module_function

  def get_tc_root
    parent = getenv("TCROOT_PARENT", MAIN_DIR)
    tcroot = getenv(
      "TCROOT", File.join(parent, DEFAULT_TC_NAME)
    )
    return Pathname.new(tcroot)
  end

  def get_host_arch(arch)

    # Translation table, necessary to handle the case where uname -m
    # returned "amd64" instead of "x86_64".
    table = {
      "amd64" => "x86_64",
      "arm64" => "aarch64"
    }

    tilck_name = table[arch]
    obj = ALL_HOST_ARCHS[arch] || ALL_HOST_ARCHS[tilck_name]

    if !obj
      puts "ERROR: host architecture #{arch} not supported"
      exit 1
    end

    return obj
  end

  def get_arch(arch)
    obj = ALL_ARCHS[arch]
    if !obj
      puts "ERROR: architecture #{arch} not supported"
      exit 1
    end
    return obj
  end

end

TC = InitOnly.get_tc_root()
TC_CACHE = TC / "cache"
ARCH = InitOnly.get_arch(getenv("ARCH", DEFAULT_ARCH))
HOST_ARCH = InitOnly.get_host_arch(Etc.uname[:machine])
HOST_ARCH_DIR_SYS = TC / "syscc" / "host_#{HOST_ARCH.name}"

DEFAULT_BOARD = ARCH.default_board
BOARD = ENV["BOARD"] || DEFAULT_BOARD
BOARD_BSP = BOARD ? MAIN_DIR / "other" / "bsp" / $ARCH.name / BOARD : nil

def get_human_arch_name(arch)
  return "noarch" if arch.nil?
  return "host" if arch == HOST_ARCH
  return arch.name
end

