# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

NINJA_SOURCE = SourceRef.new(
  name: 'ninja',
  url:  GITHUB + '/ninja-build/ninja',
)

#
# host_ninja: the build backend the whole GTK stack runs on.
#
# A :distro package, like binutils and gcc: a build tool running on the
# host, whose own linkage never reaches what it produces. It is here
# rather than taken from the system because the modern GTK stack is
# meson-based and meson needs ninja, and a build tool absent from some
# hosts and ancient on others is exactly the variance this whole effort
# exists to remove.
#
# Bootstrapped with configure.py, which compiles it with the system C++
# compiler and needs nothing but python3.
#
class HostNinjaPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_ninja',
      source: NINJA_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

  def expected_files(ver = nil) = [
    ["install/bin/ninja", false],
  ]

  # What dependents need: ninja on PATH. Published rather than left for
  # each consumer to locate.
  def build_env(ver)

    bin = install_prefix(ver) / "install" / "bin"

    return BuildEnv.new(
      bin_dirs: [bin],
      extra_env: { "NINJA" => "#{bin}/ninja" },
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def install_impl_internal(install_dir)

    ok = run_command("bootstrap.log",
                     ["python3", "./configure.py", "--bootstrap"])
    return false if !ok

    bin = "#{install_dir}/install/bin"
    FileUtils.mkdir_p(bin)
    FileUtils.cp("ninja", bin)

    prune_build_tree
    return true
  end
end

pkgmgr.register(HostNinjaPackage.new())
