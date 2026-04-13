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
      name: 'host_mtools',
      url: 'https://ftp.gnu.org/gnu/mtools',
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS,
      dep_list: [],
      default: true,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def tarname(ver) = "mtools-#{ver}.tar.gz"

  def expected_files = [
    "mtools"
  ]

  def install_impl_internal(install_dir)

    env = {}
    conf_params = [
      "--without-x",
    ]

    if OS == "FreeBSD" or OS == "Darwin"
      conf_params << "LIBS=-liconv"
      env["ac_cv_func_stat64"] = "no"
      env["ac_cv_func_lstat64"] = "no"
      env["ac_cv_func_fstat64"] = "no"
      env["ac_cv_type_struct_stat64"] = "no"
    end

    ok = with_saved_env(env.keys()) do
      run_command("configure.log", [
        "./configure", *conf_params
      ])
    end

    return false if !ok

    ok = run_command("build.log", [ "make", "-j#{BUILD_PAR}" ])
    return ok
  end
end

pkgmgr.register(MtoolsPackage.new())
