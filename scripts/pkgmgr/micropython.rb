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

# What upstream keeps as git submodules and `make submodules` would
# fetch over the network at build time: the frozen modules the unix
# port's manifest requires (micropython-lib) and the TLS library its
# ssl module is built from (mbedtls). Each at the commit the
# micropython tree pins for that version -- `git ls-tree v1.26.0
# lib/mbedtls` -- spelled through micropython's own version, so that
# the cache knows them as micropython-lib-v1.26.0.tgz and
# mbedtls-v1.26.0.tgz: mbedtls as micropython v1.26.0 has it.
# berkeley-db is a submodule too, and not needed: MICROPY_PY_BTREE=0.
MICROPYTHON_SUBMODULES = {
  Ver("v1.26.0") => {
    "lib/micropython-lib" => "34c4ee1647ac4b177ae40adf0ec514660e433dc0",
    "lib/mbedtls"         => "107ea89daaefb9867ea9121002fbbdf926780e98",
  },
}.freeze

def micropython_submodule_commit(path, ver)
  table = MICROPYTHON_SUBMODULES[Ver(ver.to_s)]
  raise "micropython #{ver}: no submodule commits known: add them to " \
        "MICROPYTHON_SUBMODULES (git ls-tree <tag> lib/)" if table.nil?
  return table.fetch(path)
end

MICROPYTHON_LIB_SOURCE = SourceRef.new(
  name:    'micropython-lib',
  url:     GITHUB + '/micropython/micropython-lib',
  git_tag: ->(ver) { micropython_submodule_commit("lib/micropython-lib", ver) },
)

MBEDTLS_SOURCE = SourceRef.new(
  name:    'mbedtls',
  url:     GITHUB + '/Mbed-TLS/mbedtls',
  git_tag: ->(ver) { micropython_submodule_commit("lib/mbedtls", ver) },
)

# Where each goes in the tree.
MICROPYTHON_SUBSOURCES = {
  "lib/micropython-lib" => MICROPYTHON_LIB_SOURCE,
  "lib/mbedtls"         => MBEDTLS_SOURCE,
}.freeze

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

  def subsources(ver = default_ver)
    return MICROPYTHON_SUBSOURCES.map { |path, src|
      Subsource.new(source: src, ver: ver, into: path)
    }
  end

  # mpy-cross is compiled for the HOST, so it must not inherit the
  # cross compiler the target build sets up -- hence `unset` rather
  # than an override.
  CC_VARS = %w[CC CXX AR NM RANLIB CROSS_PREFIX CROSS_COMPILE].freeze

  def build_steps(ver = default_ver)

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
      Within(dir: "mpy-cross", unset: CC_VARS, steps: [
        Run(log: "build.log", argv: mpy_cross),
      ]),
      Within(dir: "ports/unix", env: { "LDFLAGS_EXTRA" => "-static" },
             steps: [
        Run(log: "build.log", argv: unix_port),
      ]),
    ]
  end
end

pkgmgr.register(MicropythonPackage.new())
