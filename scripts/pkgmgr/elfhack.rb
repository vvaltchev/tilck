# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# The version is the commit to build (HOST_VER_ELFHACK): elfhack has no
# releases, and a hash is a version of its own (VersionType::HASH), cloned
# as it is and pinned in other/pkg_hashes.
#
ELFHACK_SOURCE = SourceRef.new(
  name: 'elfhack',
  url:  GITHUB + '/vvaltchev/elfhack',
)

#
# elfhack: the tool the build uses to post-process ELF binaries (the
# kernel's symbol tables, the legacy bootloader's flat stage 3, the objects
# whose symbols the unit tests wrap). One binary per ELF class, elfhack32
# and elfhack64, whatever the host's bitness. Plain C linking only libc:
# installed in the distro tier, like mtools.
#
# The host compiler is passed to cmake explicitly, as for gtest: HOST_CC_CMD
# owns the answer (on FreeBSD, `cc` is clang).
#
class ElfhackPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_elfhack',
      source: ELFHACK_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [],
      default: true,
    )
  end

  def expected_files(ver = nil) = [
    ["install/bin/elfhack32", false],
    ["install/bin/elfhack64", false],
  ]

  def build_steps(ver = default_ver) = [
    Mkdir(path: "build"),
    Within(dir: "build", steps: [
      Run(log: "cmake.log", argv: [
        "cmake",
        "-DCMAKE_C_COMPILER=#{HOST_CC_CMD}",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_PREFIX=$INSTALL/install",
        "-DCMAKE_INSTALL_BINDIR=bin",
        "..",
      ]),
      Run(log: "build.log", argv: ["make", "-j$PAR"]),
      Run(log: "install.log", argv: ["cmake", "--install", "."]),
    ]),
    Prune(),
  ]
end

pkgmgr.register(ElfhackPackage.new())
