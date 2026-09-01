# SPDX-License-Identifier: BSD-2-Clause

# TinyCC cross-compilation for Tilck.
#
# The fundamental challenge is a triple-cross scenario:
#
#   Build host:             x86_64-linux
#   TCC will run on:        i386-linux (Tilck)
#   TCC binaries target:    i386-linux (Tilck)
#
# TCC's build system conflates "host" and "target", making true
# cross-compilation difficult. Two problems must be solved:
#
#   1. c2str.exe: a host tool that TCC tries to build with $(CC), which
#      during cross builds is the cross-compiler. Fix: patch the Makefile
#      to use the system gcc for c2str.
#
#   2. libtcc1.a: the TCC runtime library, normally built by running the
#      just-built tcc binary. In a cross build, that binary is i386 and
#      cannot run on the x86_64 host. Fix: pass i386-libtcc1-usegcc=yes
#      to make, which uses the cross-GCC instead.
#
# The old bash script worked around problem #2 by installing 32-bit glibc
# on the host and running the i386 binary via the kernel's compat layer.
# This was fragile and broke with GCC >= 10.3. The usegcc approach avoids
# executing cross-compiled binaries entirely.

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# TCC lives on repo.or.cz, not github — the SourceRef heuristic
# defaults to HTTP download for non-github URLs, so we explicitly
# opt into git clone via fetch_via_git: true.
#
TCC_SOURCE = SourceRef.new(
  name: 'tcc',
  url:  "git://repo.or.cz/tinycc.git",
  fetch_via_git: true,
)

class TccPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'tcc',
      source: TCC_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: Archs("i386", "x86_64", "riscv64"),
      dep_list: []
    )
  end

  def expected_files(ver = nil) = [
    ["tcc",               false],
    ["libtcc1.a",         false],
    ["include",           true],
    ["tcclib.h",          false],
    ["examples",          true],
  ]

  #
  # DEF_GITHASH is passed explicitly, and has to be. tcc's Makefile
  # computes one itself by shelling out to git:
  #
  #   GITHASH := $(shell git log -1 --date=short --pretty='format:%cd ...')
  #   GITMODF := $(shell git diff --quiet || echo '*')
  #
  # and the cached tarball still carries a .git, so that works -- it
  # would bake a commit DATE into the version string, and append a "*"
  # because the tree is dirty: we patch it. A command-line variable
  # beats a makefile assignment, so passing it pins the value to the
  # ref the source was fetched at.
  #
  # $SRC_REF rather than reading .ref_short here: build_steps is asked
  # for during a staleness check too, when no source tree exists to
  # read it from.
  #
  # CC and AR are unset for configure alone. tcc's configure picks CC
  # up from the environment and then --cross-prefix prepends to it
  # again, so it has to start from a bare "gcc"/"ar".
  #
  # No -j: this build is not parallel-safe, and was not run that way
  # before either.
  #
  def build_steps

    arch = default_arch.gcc_tc    # "i686" or "riscv64"
    cpu = default_arch.name       # "i386" or "riscv64"

    # Where TCC looks for crt*.o and libraries at runtime, on Tilck.
    tilck_lib = "/lib/#{arch}-tilck-musl"

    configure = [
      "./configure",
      "--cross-prefix=#{arch}-linux-",
      "--cpu=#{cpu}",
      "--enable-static",
      "--config-bcheck=no",
      "--config-backtrace=no",
      "--prefix=/",
      "--extra-ldflags=-static",
      "--crtprefix=#{tilck_lib}",
      "--libpaths=#{tilck_lib}",
    ]

    # macOS: configure auto-detects Darwin and sets CONFIG_OSX, adding
    # macOS-only linker flags (-flat_namespace) that the GNU cross
    # linker rejects. We are cross-compiling for Tilck.
    configure << "--targetos=Linux" if OS == "Darwin"

    return [
      Step("configure.log", configure, unset: %w[CC AR]),

      # <cpu>-libtcc1-usegcc=yes makes lib/Makefile compile libtcc1.a
      # with $(CC), the cross GCC, instead of running the tcc it has
      # just built on the build host.
      Step("build.log", [
        "make",
        "#{cpu}-libtcc1-usegcc=yes",
        "DEF_GITHASH=-DTCC_GITHASH=\\\"$SRC_REF\\\"",
      ]),

      Step("strip.log", ["#{arch}-linux-strip", "--strip-all", "tcc"]),
    ]
  end

end

pkgmgr.register(TccPackage.new())
