# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class NcursesPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'ncurses',
      url: 'https://ftp.gnu.org/pub/gnu/ncurses',
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def tarname(ver) = "ncurses-#{ver}.tar.gz"

  def expected_files = [
    ["install/lib/libncurses.a", false]
  ]

  def install_impl_internal(install_dir)

    arch = default_arch().gcc_tc

    ok = run_command("configure.log", [
      "./configure",
      "--host=#{arch}-pc-linux-gnu",
      "--prefix=#{install_dir}/install",
      "--datarootdir=/usr/share",
      "--disable-db-install",
      "--disable-widec",
      "--without-progs",
      "--without-cxx",
      "--without-cxx-binding",
      "--without-ada",
      "--without-manpages",
      "--without-dlsym",
    ])
    return false if !ok

    ok = run_command("build.log", [ "make", "-j#{BUILD_PAR}" ])
    return false if !ok

    ok = run_command("install.log", [ "make", "install" ])
    return ok
  end
end

pkgmgr.register(NcursesPackage.new())
