# SPDX-License-Identifier: BSD-2-Clause

require 'power_assert'
require 'pathname'
require_relative 'arch'

DEFAULT_TC_NAME = "toolchain3"
RUBY_SOURCE_DIR = Pathname.new(File.realpath(__dir__))
MAIN_DIR = Pathname.new(RUBY_SOURCE_DIR.parent.parent)

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
ARCH = InitOnly.get_arch(getenv("ARCH", DEFAULT_ARCH))
HOST_ARCH = InitOnly.get_host_arch(Etc.uname[:machine])
HOST_ARCH_DIR_SYS = TC / "syscc" / "host_#{HOST_ARCH.name}"

DEFAULT_BOARD = ARCH.default_board
BOARD = ENV["BOARD"] || DEFAULT_BOARD
BOARD_BSP = BOARD ? MAIN_DIR / "other" / "bsp" / $ARCH.name / BOARD : nil
