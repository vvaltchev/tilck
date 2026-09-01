# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

MICROPYTHON_SOURCE = SourceRef.new(
  name: 'micropython',
  url:  GITHUB + '/micropython/micropython',
)

class MicropythonPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'micropython',
      source: MICROPYTHON_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS.values,
      dep_list: []
    )
  end

  def expected_files(ver = nil) = [
    ["ports/unix/build-standard/micropython", false],
  ]

  # mpy-cross is compiled for the HOST, so it must not inherit the
  # cross compiler the target build sets up -- hence `unset` rather
  # than an override.
  CC_VARS = %w[CC CXX AR NM RANLIB CROSS_PREFIX CROSS_COMPILE].freeze

  def build_steps

    mpy_cross = ["make", "V=1", "-j$PAR"]

    unix_port = [
      "make", "V=1",
      "MICROPY_PY_FFI=0",
      "MICROPY_PY_THREAD=0",
      "MICROPY_PY_BTREE=0",
      "-j$PAR",
    ]

    if OS == "Darwin"
      # Clang treats the VLA-folded-to-constant-array idiom used by
      # MP_STATIC_ASSERT as -Werror,-Wgnu-folding-constant.
      mpy_cross << "CFLAGS_EXTRA=-Wno-error=gnu-folding-constant"

      # The unix port Makefile detects Darwin and forces CC=clang plus
      # macOS-specific linker flags (-Wl,-dead_strip). We cross-compile
      # for Linux/Tilck with the GNU toolchain, so tell it we are on
      # Linux and let it take the right path entirely.
      unix_port << "UNAME_S=Linux"
    end

    return [
      Step("build.log", mpy_cross, dir: "mpy-cross", unset: CC_VARS),
      Step("make_submodules.log", ["make", "submodules"],
           dir: "ports/unix"),
      Step("build.log", unix_port, dir: "ports/unix",
           env: { "LDFLAGS_EXTRA" => "-static" }),
    ]
  end
end

pkgmgr.register(MicropythonPackage.new())
