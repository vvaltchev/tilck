# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'gcc'

require 'pathname'
require 'fileutils'

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

def check_gcc_tc_ver

  failures = 0
  for name, arch in ALL_ARCHS do

    v = arch.gcc_ver
    min = arch.min_gcc_ver

    if v && v < min
      puts "ERROR: [arch #{name}] gcc ver #{v} < required #{min}"
      failures += 1
    end
  end

  if failures > 0
    puts
    puts "Steps to fix:"
    puts
    puts "   1. unset \$GCC_TC_VER"
    puts "   2. ./scripts/build_toolchain --clean"
    puts "   3. rm -rf build # or any other build directory"
    puts "   4. ./scripts/build_toolchain"
    puts
    exit 1
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

def early_checks
  if !(MAIN_DIR.to_s.index ' ').nil?
    puts "ERROR: Tilck must be checked out in a path *WITHOUT* spaces"
    puts "Project's root dir: '#{MAIN_DIR}'"
    exit 1
  end
  if BOARD && !BOARD_BSP.exist?
    puts "ERROR: BOARD_BSP: #{BOARD_BSP} not found!"
    exit 1
  end
end

def create_toolchain_dirs
  for name, arch in ALL_ARCHS do
    FileUtils.mkdir_p(TC / arch.gcc_ver._ / name)
  end
  for compiler in [ HOST_ARCH.gcc_ver._, "syscc" ] do
    FileUtils.mkdir_p(TC / compiler / "host_#{HOST_ARCH.name}")
  end
end

def main(argv)

  early_checks
  read_gcc_ver_defaults
  set_gcc_tc_ver
  check_gcc_tc_ver
  create_toolchain_dirs

  dump_context

  puts
  PackageManager.instance.show_status_all

  return 0
end

if $PROGRAM_NAME == __FILE__
  exit main(ARGV)
end
