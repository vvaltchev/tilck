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
      dep_list: [Dep('host_python', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

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

  # configure.py --bootstrap leaves ./ninja in the source tree and has
  # no install target, so the copy is the install.
  #
  # `install -D` would do both steps at once, but only GNU coreutils
  # has -D: this package is built on FreeBSD and macOS hosts too, whose
  # install(1) does not. mkdir + cp is the portable pair.
  def build_steps = [
    # OUR python, not whichever one PATH offers. deps_build_env puts
    # host_python's bin dir at the front, so "python3" resolves to it
    # -- but $PYTHON says which one was meant, and a build that finds
    # a different interpreter than the one it declared is exactly the
    # ambiguity this dependency exists to remove.
    Step("bootstrap.log", ["$PYTHON", "./configure.py", "--bootstrap"]),
    Step("mkdir.log", ["mkdir", "-p", "$INSTALL/install/bin"]),
    Step("install.log", ["cp", "ninja", "$INSTALL/install/bin/ninja"]),
  ]

  def prune_after_build? = true
end

pkgmgr.register(HostNinjaPackage.new())
