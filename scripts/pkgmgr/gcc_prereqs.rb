# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# GCC's four maths libraries: gmp, mpfr, mpc and isl.
#
# GCC can fetch and build these itself -- contrib/download_prerequisites
# downloads them from gcc.gnu.org and drops them in its own source tree
# for an in-tree build. That is one fewer thing to package, and it cost
# us a compiler build the first time gcc.gnu.org failed to resolve:
#
#   wget: unable to resolve host address 'gcc.gnu.org'
#   error: Cannot download gmp-6.2.1.tar.bz2
#
# Nothing else the package manager builds behaves that way. Every other
# source is fetched once into the cache, resumably, and every later
# build is offline. So these are packages like any other, and GCC is
# pointed at them with --with-gmp and friends -- the configuration
# upstream documents for exactly this.
#
# All four are :distro. They are built by the system compiler and get
# linked into cc1, which is a build tool running on this host; what
# they link against never reaches anything the compiler produces.
#
# WHICH version belongs to which GCC is not decided here. It is
# HostGccPackage::PREREQS, because it is a fact about a GCC, and
# test_gcc_prereqs.rb checks that table against what each cached GCC
# tarball's own download_prerequisites says.
#
# Fetched from GCC's infrastructure mirror rather than each project's
# own home: it carries exactly the versions GCC pins, including isl,
# which is not a GNU project and whose old releases are awkward to
# find elsewhere.
#
GCC_INFRA = 'https://gcc.gnu.org/pub/gcc/infrastructure'

GMP_SOURCE = SourceRef.new(
  name: 'gmp',
  url:  GCC_INFRA,
  tarname: ->(ver) { "gmp-#{ver}.tar.bz2" },
)

MPFR_SOURCE = SourceRef.new(
  name: 'mpfr',
  url:  GCC_INFRA,
  tarname: ->(ver) { "mpfr-#{ver}.tar.bz2" },
)

MPC_SOURCE = SourceRef.new(
  name: 'mpc',
  url:  GCC_INFRA,
  tarname: ->(ver) { "mpc-#{ver}.tar.gz" },
)

ISL_SOURCE = SourceRef.new(
  name: 'isl',
  url:  GCC_INFRA,
  tarname: ->(ver) { "isl-#{ver}.tar.bz2" },
)

#
# What the four have in common: a plain autotools build, installed into
# their own prefix, static only.
#
# Static because of what consumes them. GCC links these into cc1 and
# friends; a shared build would leave the compiler needing four .so
# files at run time, found through a search path we would then have to
# keep true for every stack. Nothing else uses them, so there is no
# case for the shared variant.
#
class GccPrereqPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  # The prefix a dependent passes to --with-<name>.
  def prefix_for(ver) = install_prefix(ver) / "install"

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  # Where a named dependency of THIS package was installed.
  #
  # From the resolution of the request being installed, NOT from this
  # package's own dep list: mpfr names host_gmp without a version, so
  # asking mpfr alone answers "gmp's default" -- while the gcc that
  # asked for all four pinned something else. gcc 16 pins gmp 6.3.0,
  # and mpfr must link the same one, not 6.2.1.
  def dep_prefix(name, ver)
    pkg = pkgmgr.get(name)
    return pkg.prefix_for(pkgmgr.resolved_ver(name) || pkg.default_ver)
  end

  # Every one of them: out-of-tree configure, make, make install into
  # DESTDIR, then lift the tree into place.
  def install_impl_internal(install_dir)

    ver = installing_ver(install_dir)
    prefix = final_install_prefix(install_dir)
    destdir = "#{install_dir}/destdir"

    FileUtils.mkdir_p("build")

    conf = [
      "../configure",
      "--prefix=#{prefix}",
      "--disable-shared",
      "--enable-static",
      *configure_flags(ver),
    ]

    ok = false

    chdir("build") do
      ok = run_command("configure.log", conf) &&
           run_command("build.log", ["make", "-j#{BUILD_PAR}"]) &&
           run_command("install.log",
                       ["make", "install", "DESTDIR=#{destdir}"])
    end

    return false if !ok

    # Move the staged prefix into place as a whole. NOT "#{...}/." --
    # that is cp's trailing-dot idiom and FileUtils.mv refuses it when
    # the destination exists.
    FileUtils.mv("#{destdir}#{prefix}", "#{install_dir}/install")
    prune_build_tree
    return true
  end

  # What this one needs told about the others.
  def configure_flags(ver) = []
end

class HostGmpPackage < GccPrereqPackage

  def initialize
    super(
      name: 'host_gmp',
      source: GMP_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [],
      default: false,
    )
  end

  def expected_files(ver = nil) = [
    ["install/lib/libgmp.a", false],
    ["install/include/gmp.h", false],
  ]
end

class HostMpfrPackage < GccPrereqPackage

  def initialize
    super(
      name: 'host_mpfr',
      source: MPFR_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gmp', true)],
      default: false,
    )
  end

  def expected_files(ver = nil) = [
    ["install/lib/libmpfr.a", false],
    ["install/include/mpfr.h", false],
  ]

  def configure_flags(ver) = ["--with-gmp=#{dep_prefix('host_gmp', ver)}"]
end

class HostMpcPackage < GccPrereqPackage

  def initialize
    super(
      name: 'host_mpc',
      source: MPC_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gmp', true), Dep('host_mpfr', true)],
      default: false,
    )
  end

  def expected_files(ver = nil) = [
    ["install/lib/libmpc.a", false],
    ["install/include/mpc.h", false],
  ]

  def configure_flags(ver) = [
    "--with-gmp=#{dep_prefix('host_gmp', ver)}",
    "--with-mpfr=#{dep_prefix('host_mpfr', ver)}",
  ]
end

class HostIslPackage < GccPrereqPackage

  def initialize
    super(
      name: 'host_isl',
      source: ISL_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gmp', true)],
      default: false,
    )
  end

  def expected_files(ver = nil) = [
    ["install/lib/libisl.a", false],
    ["install/include/isl/version.h", false],
  ]

  # isl spells it differently from the other three.
  def configure_flags(ver) = [
    "--with-gmp-prefix=#{dep_prefix('host_gmp', ver)}",
  ]
end

pkgmgr.register(HostGmpPackage.new())
pkgmgr.register(HostMpfrPackage.new())
pkgmgr.register(HostMpcPackage.new())
pkgmgr.register(HostIslPackage.new())
