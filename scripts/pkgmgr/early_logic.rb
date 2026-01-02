# SPDX-License-Identifier: BSD-2-Clause

require 'power_assert'
require 'pathname'
require 'etc'
require 'file'

require_relative 'arch'

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

def mkpath(path_str) = Pathname.new(path_str)

def make_gh_rel_download(user, proj, tag)
  return "#{GITHUB}/#{user}/#{proj}/releases/download/#{tag}"
end

module FileShortcuts
  def exist?(p)      = File.exist?(p)
  def file?(p)       = File.file?(p)
  def symlink?(p)    = File.symlink?(p)
  def directory?(p)  = File.directory?(p)
  def readable?(p)   = File.readable?(p)
  def writable?(p)   = File.writable?(p)
  def executable?(p) = File.executable?(p)
  def basename(p)    = File.basename(p)
  def dirname(p)     = File.dirname(p)
  def extname(p)     = File.extname(p)
  def stat(p)        = File.stat(p)
  def lstat(p)       = File.lstat(p)
  def readlink(p)    = File.readlink(p)
  def expand_path(p) = File.expand_path(p)
  def realpath(p)    = File.realpath(p)
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
      "amd64" => "x86_64"
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
