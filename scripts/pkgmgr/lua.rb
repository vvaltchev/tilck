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
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def expected_files = [
    ["src/lua", false],
    ["src/luac", false],
  ]

  def clean_build(dir)
    system("make", "clean", chdir: dir.to_s,
           out: "/dev/null", err: "/dev/null")
  end

  def install_impl_internal(install_dir)

    # Drop the linker's `-Wl,-E` (export-dynamic) from src/Makefile —
    # we link statically against musl, so the export table is meaningless
    # and binutils-ld for the cross targets refuses the option.
    src_mk = File.read("src/Makefile")
    src_mk.gsub!("-Wl,-E", "")
    File.write("src/Makefile", src_mk)

    # Force the platform to "linux" instead of relying on the host's
    # `make guess` (which would pick up the build host's OS — wrong on
    # macOS/FreeBSD when cross-compiling for Tilck).
    top_mk = File.read("Makefile")
    top_mk.gsub!("PLAT= guess", "PLAT= linux")
    File.write("Makefile", top_mk)

    ok = run_command("build.log", [
      "make",
      "-j#{BUILD_PAR}",
      "CC=#{ENV["CC"]}",
      "MYCFLAGS=-std=gnu99",
      "AR=#{ENV["AR"]} rcu",
      "RANLIB=#{ENV["RANLIB"]}",
    ])
    return ok
  end
end

pkgmgr.register(LuaPackage.new())
