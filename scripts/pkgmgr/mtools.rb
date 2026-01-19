# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class MtoolsPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'mtools',
      url: 'https://ftp.gnu.org/gnu/mtools',
      on_host: true,
      is_compiler: false,
      arch_list: ALL_HOST_ARCHS,
      dep_list: []
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def tarname(ver) = "mtools-#{ver}.tar.gz"

  def expected_files = [
    "mtools"
  ]

  def install_impl_internal(install_subdir)
    conf_params = []

    if OS == "FreeBSD"
      conf_params << "LIBS=-liconv"
    end

    ok = run_command("configure.log", [
      "./configure",
      *conf_params
    ])
    return false if !ok

    ok = run_command("build.log", [ "make", "-j#{BUILD_PAR}" ])
    return ok
  end
end

pkgmgr.register(MtoolsPackage.new())
