# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

BINUTILS_SOURCE = SourceRef.new(
  name: 'binutils',
  url:  'https://ftp.gnu.org/gnu/binutils',
  tarname: ->(ver) { "binutils-#{ver}.tar.xz" },
)

#
# host_binutils: the assembler, linker and object tools the hermetic
# toolchain is built with and builds through.
#
# A :distro package, not a hermetic one. Its binaries are built by the
# system compiler and link against the system libc, and the tier
# describes what a package's own binaries depend on. That is fine:
# these are build tools running on the host, and what they are linked
# against does not reach anything they produce. Hermeticity belongs to
# the sysroot they target.
#
# One binutils therefore serves every hermetic stack rather than being
# rebuilt per compiler version. A GCC needing a particular binutils
# pins it: Dep('host_binutils', true, ver: ...).
#
# --with-sysroot is not optional even though the value is only a
# default: GNU ld rejects the runtime --sysroot flag unless it was
# configured with --with-sysroot, and that runtime flag is how GCC
# points ld at the right stack. The default names the hermetic base,
# which exists but holds no usr/lib, so a bare `ld` invoked outside GCC
# fails to find libraries rather than silently falling back to
# /usr/lib. Failing closed is the point.
#
# See docs/plans/hermetic-host-toolchain.md.
#
class HostBinutilsPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_binutils',
      source: BINUTILS_SOURCE,
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
    ["install/bin/ld", false],
    ["install/bin/as", false],
    ["install/bin/ar", false],
    ["install/bin/ranlib", false],
    ["install/bin/objdump", false],
    ["install/bin/strip", false],
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  def install_impl_internal(install_dir)

    # Configure with the path this will live at once installed, NOT the
    # staging path we are standing in: ld bakes its library search dirs
    # and ldscripts location into itself from --prefix, and the staging
    # directory stops existing the moment the install completes.
    prefix = final_install_prefix(install_dir)
    destdir = "#{install_dir}/destdir"

    # Binutils insists on being configured outside its source tree.
    FileUtils.mkdir_p("build")

    conf = [
      "../configure",
      "--prefix=#{prefix}",
      "--with-sysroot=#{HOST_DIR_HERMETIC_BASE}",

      # No translations: they would pull in the host's gettext, and
      # nothing here is user-facing enough to want them.
      "--disable-nls",

      # Recent GCC warns about things older binutils sources trip over;
      # those warnings are not ours to fix.
      "--disable-werror",

      # Byte-identical archives across rebuilds: no timestamps, uids or
      # gids recorded. Cheap, and it keeps rebuild comparisons honest.
      "--enable-deterministic-archives",
    ]

    ok = false
    chdir("build") do
      # MAKEINFO=true: the docs need texinfo, which is not worth
      # requiring on the host for a tool nobody reads the info pages of.
      ok = run_command("configure.log", conf + ["MAKEINFO=true"])
      next if !ok

      ok = run_command("build.log", ["make", "-j#{BUILD_PAR}"])
      next if !ok

      # ...and stage it through DESTDIR, so the tree we hand to the
      # atomic move is complete while the paths inside it describe
      # where it is going.
      ok = run_command("install.log",
                       ["make", "install", "MAKEINFO=true",
                        "DESTDIR=#{destdir}"])
    end

    return false if !ok

    # DESTDIR reproduces the whole absolute prefix beneath it; lift the
    # tree back out to where the atomic move expects it.
    FileUtils.mv("#{destdir}#{prefix}", "#{install_dir}/install")

    # The deliverable is the install prefix; the source and the build
    # tree together are several hundred MB of no further use.
    Dir.children(".").each { |e|
      next if e == "install"
      rm_rf(e)
    }
    return true
  end
end

pkgmgr.register(HostBinutilsPackage.new())
