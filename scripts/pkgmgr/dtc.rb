# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# DTC (Device Tree Compiler) — we only need libfdt.a and its headers,
# linked into the kernel for archs that boot via a flattened device tree
# (currently riscv64).
#
class DtcPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'dtc',
      url: 'https://mirrors.edge.kernel.org/pub/software/utils/dtc',
      on_host: false,
      is_compiler: false,
      arch_list: { "riscv64" => ALL_ARCHS["riscv64"] },
      dep_list: [],
      default: true,
    )
  end

  def tarname(ver) = "dtc-#{ver}.tar.gz"

  def expected_files = [
    ["libfdt/libfdt.a", false],
    ["libfdt/libfdt.h", false],
  ]

  def install_impl_internal(install_dir)

    # Build only the static archive — `make libfdt` would also build the
    # host shared library (libfdt-VER.dylib on macOS) using the *host*
    # uname's link flags (-install_name), which the Linux cross-linker
    # rejects. The kernel only links libfdt.a, so the .so/.dylib is
    # unneeded.
    ok = run_command("build.log", [
      "make",
      "libfdt/libfdt.a",
      "V=1",
      "-j#{BUILD_PAR}",
      "STATIC_BUILD=1",
      "EXTRA_CFLAGS=-fno-stack-protector",
    ])
    return ok
  end
end

pkgmgr.register(DtcPackage.new())
