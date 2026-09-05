# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

GPERF_SOURCE = SourceRef.new(
  name: 'gperf',
  url:  'https://ftp.gnu.org/gnu/gperf',
  tarname: ->(ver) { "gperf-#{ver}.tar.gz" },
)

#
# host_gperf: the perfect-hash generator libseccomp's build runs.
#
# libseccomp generates its syscall table with gperf and its configure
# refuses to go on without one ("please install gperf"). Nothing
# declared that, so the build passed on a host that happened to have
# gperf and stopped on one that did not -- the variance this stack
# exists to remove, found on the first machine that was not the one
# it was written on.
#
# A :distro package, like ninja and meson: a build tool running on the
# host, whose own linkage never reaches what it produces. Packaged
# rather than asked of the distro because it is one C++ program that
# builds in seconds, which is not a reason to depend on every distro
# spelling its name the same way.
#
class HostGperfPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_gperf',
      source: GPERF_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

  def expected_files(ver = nil) = [
    ["install/bin/gperf", false],
  ]

  # What dependents need: gperf on PATH.
  def build_env(ver)
    return BuildEnv.new(bin_dirs: [install_prefix(ver) / "install" / "bin"])
  end

  def build_steps = [
    Step("configure.log", ["./configure", "--prefix=$INSTALL/install"]),
    Step("build.log", ["make", "-j$PAR"]),
    Step("install.log", ["make", "install"]),
  ]

  def prune_after_build? = true
end

pkgmgr.register(HostGperfPackage.new())
