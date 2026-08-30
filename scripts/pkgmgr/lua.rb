# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LUA_SOURCE = SourceRef.new(
  name: 'lua',
  url:  'https://www.lua.org/ftp',
  tarname: ->(ver) { "lua-#{ver}.tar.gz" },
)

class LuaPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'lua',
      source: LUA_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS.values,
      dep_list: []
    )
  end

  def expected_files(ver = nil) = [
    ["src/lua", false],
    ["src/luac", false],
  ]

  def clean_build(dir)
    system("make", "clean", chdir: dir.to_s,
           out: "/dev/null", err: "/dev/null")
  end

  # The two edits this build used to make in place -- dropping
  # `-Wl,-E` from src/Makefile and pinning PLAT to linux -- are
  # patches now. Both are what the base class already applies and
  # already fingerprints, and patch(1) fails loudly where a gsub that
  # matches nothing succeeds silently.
  #
  # The toolchain is named from the arch rather than read out of CC,
  # AR and RANLIB, because build_steps is asked for during a staleness
  # check too, when no build is running and the environment holds
  # nothing. It is the same value: with_cc sets CC to exactly this.
  def build_steps

    arch = default_arch().gcc_tc

    return [
      Step("build.log", [
        "make",
        "-j$PAR",
        "CC=#{arch}-linux-gcc",
        "MYCFLAGS=-std=gnu99",
        "AR=#{arch}-linux-ar rcu",
        "RANLIB=#{arch}-linux-ranlib",
      ]),
    ]
  end
end

pkgmgr.register(LuaPackage.new())
