# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# Which python-build-standalone release each CPython comes from.
#
# Their assets carry both numbers -- cpython-3.11.16+20260901-... --
# because one interpreter version is republished whenever the build
# recipe changes. The table keeps the pair together, so bumping
# HOST_VER_PYTHON is one edit here and one in other/host_pkg_versions
# rather than a version that silently keeps an old build.
#
PYTHON_BUILDS = {
  Ver("3.11.16") => "20260901",
}.freeze

# The host triple those assets are named by. Not our Architecture
# names: this is their naming, and mapping into it here keeps the
# mapping in one place.
PYTHON_TRIPLES = {
  ["Linux",  "x86_64"]  => "x86_64-unknown-linux-gnu",
  ["Linux",  "aarch64"] => "aarch64-unknown-linux-gnu",
  ["Darwin", "x86_64"]  => "x86_64-apple-darwin",
  ["Darwin", "aarch64"] => "aarch64-apple-darwin",
}.freeze

def python_triple = PYTHON_TRIPLES[[OS, HOST_ARCH.name]]

def python_asset(ver)
  build = PYTHON_BUILDS[Ver(ver.to_s)]
  raise "no python-build-standalone release known for CPython " \
        "#{ver}: add it to PYTHON_BUILDS" if build.nil?
  return "cpython-#{ver}+#{build}-#{python_triple}-install_only.tar.gz"
end

PYTHON_SOURCE = SourceRef.new(
  name: 'python',
  url: GITHUB + '/astral-sh/python-build-standalone/releases/download',
  tarname: ->(ver) { python_asset(ver) },
  remote_tarname: ->(ver) {
    "#{PYTHON_BUILDS[Ver(ver.to_s)]}/#{python_asset(ver)}"
  },
  fetch_via_git: false,
)

# Pure-Python wheels installed into it, from the cache rather than
# from PyPI at build time. The path form without the hash directory
# is the stable one and redirects to the hashed asset, which our
# downloader follows -- so a rebuild works offline like every other
# source in the tree.
PYTHON_WHEELS = {
  "distlib" => {
    ver: "0.3.9",
    file: "distlib-0.3.9-py2.py3-none-any.whl",
    url: "https://files.pythonhosted.org/packages/py2.py3/d/distlib",
  },
}.freeze

#
# host_python: the interpreter our builds run, instead of whichever
# one happens to be first on PATH.
#
# QEMU 8 and later create a Python venv during configure and need
# distlib to populate it. Installing python3-distlib as a system
# package put it under /usr/bin/python3 while configure searched PATH
# and found a Homebrew 3.14 without it:
#
#   python determined to be '/home/linuxbrew/.linuxbrew/bin/python3'
#   *** Ouch! ***
#   found no usable distlib, please install it
#
# Pointing the build at the distro's interpreter instead only moved
# the problem: 3.10 has no tomllib, and QEMU reads pythondeps.toml
# with it. Two interpreters on one machine, each missing a different
# thing the build needs, is what happens when the build does not
# choose.
#
# So this is a package like any other. Every other input to a Tilck
# build comes from somewhere we chose -- the compiler, the libraries,
# the sysroot -- and the interpreter that runs the build scripts is
# no different.
#
# PREBUILT, not compiled. python-build-standalone publishes
# self-contained CPython for exactly the platforms this stack runs on,
# and building CPython from source to run a build script is a cost
# with nothing to show for it. The tree already downloads prebuilt
# cross compilers on the same reasoning.
#
# 3.11 specifically, because it straddles the QEMU range this tree
# builds: tomllib is stdlib from 3.11, so the tomli backport is never
# needed, and distutils still exists (it went in 3.12) for the older
# QEMU build scripts that still reach for it.
#
class HostPythonPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_python',
      source: PYTHON_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

  # No asset for this platform means no package, rather than a
  # download that 404s halfway through an install. FreeBSD has no
  # python-build-standalone build; it also has no glibc, so the host
  # stack this belongs to does not run there either.
  def enabled? = !python_triple.nil?

  # 3.11.16 -> "python3.11", which is what the tree inside the
  # tarball is named and where pip puts what it installs.
  def libdir(ver) = "python#{Ver(ver.to_s).comps[0, 2].join(".")}"

  def expected_files(ver = nil) = [
    ["bin/python3", false],
    ["lib/#{libdir(ver || default_ver)}/site-packages/distlib/__init__.py",
     false],
  ]

  # Published as a bin dir, not named by anyone.
  #
  # deps_build_env puts what a package's dependencies publish at the
  # front of PATH, so a consumer gets this interpreter by asking for
  # "python3" -- which is exactly what QEMU's configure does when it
  # goes looking. Declaring Dep('host_python', true) is the whole of
  # the consumer's side; no package names this one.
  def build_env(ver)
    return BuildEnv.new(bin_dirs: [install_prefix(ver) / "bin"])
  end

  def install_impl_internal(install_dir)

    py = install_dir / "bin" / "python3"

    for name, w in PYTHON_WHEELS do
      Cache.download_file(w[:url], w[:file])

      # --no-index and --no-deps: everything comes from the cache, and
      # a wheel quietly pulling a dependency off PyPI would be exactly
      # the reach outside the toolchain this package exists to stop.
      ok = run_command("pip-#{name}.log",
                       [py.to_s, "-m", "pip", "install",
                        "--no-index", "--no-deps", "--no-warn-script-location",
                        (TC_CACHE / w[:file]).to_s])
      return false if !ok
    end

    return true
  end
end

pkgmgr.register(HostPythonPackage.new())
