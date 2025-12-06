# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require 'pathname'

RUBY_SOURCE_DIR = Pathname.new(File.realpath(__dir__))
MAIN_DIR = Pathname.new(RUBY_SOURCE_DIR.parent.parent)
TC = InitOnly.get_tc_root()

def read_gcc_ver_defaults
  conf = MAIN_DIR / "other/gcc_tc_conf"
  for name, arch in ALL_ARCHS do
    arch.min_gcc_ver = Ver(File.read(conf / name / "min_ver"))
    arch.default_gcc_ver = Ver(File.read(conf / name / "default_ver"))
    arch.gcc_ver = arch.default_gcc_ver
  end
end

def set_gcc_tc_ver

  ver = Ver(getenv("GCC_TC_VER", ARCH.default_gcc_ver))
  ALL_ARCHS[ARCH.name].gcc_ver = ver

  if ARCH.family == "generic_x86"
     # Special case for x86: since we're downloading both toolchains
     # also to be used for Tilck (bootloader), not just for the host
     # apps, it makes sense to force GCC_TC_VER to also apply for the
     # other architecture. In general case (e.g. riscv64, aarch64) that
     # won't happen, as we need only *one* GCC toolchain for Tilck and
     # one for the host apps.
    ALL_ARCHS["i386"].gcc_ver = ver
    ALL_ARCHS["x86_64"].gcc_ver = ver
  end

  for name, arch in ALL_ARCHS do
    arch.target_dir = TC / ver._ / name
    arch.host_dir = TC / ver._ / "host_#{name}"
    arch.host_syscc_dir = TC / "syscc" / "host_#{name}"
  end
end

def dump_context

  def de(x)
    (x.start_with? "ENV:") ? ENV[x[4..]] : Object.const_get(x).to_s
  end

  list = %w[
    ENV:GCC_TC_VER
    ENV:CC
    ENV:CXX
    ENV:ARCH
    ENV:BOARD
    MAIN_DIR
    TC
    HOST_ARCH
    ARCH
    BOARD
    DEFAULT_BOARD
  ]

  list.each { |x| puts "#{x} = #{de(x)}" }
  for k, v in ALL_ARCHS do
    puts "GCC_VER[#{k}]: #{v.gcc_ver}"
  end
end

def main(argv)

  if !(MAIN_DIR.to_s.index ' ').nil?
    puts "ERROR: Tilck must be checked out in a path *WITHOUT* spaces"
    puts "Project's root dir: '#{MAIN_DIR}'"
    return 1
  end

  read_gcc_ver_defaults
  set_gcc_tc_ver

  if BOARD && !BOARD_BSP.exist?
    puts "ERROR: BOARD_BSP: #{BOARD_BSP} not found!"
    return 1
  end

  dump_context
  puts "args: ", argv
  return 0
end

if $PROGRAM_NAME == __FILE__
  exit main(ARGV)
end
