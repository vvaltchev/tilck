# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# fbDOOM — framebuffer port of DOOM. Upstream has no release tags — the
# ver string is used only as a cache key / staging dir name. Runtime
# data (the freedoom WAD) comes from the `freedoom` package, declared
# here as a dep so the install plan pulls both.
#
# Produces `<install>/fbdoom.gz` — a single stripped+compressed
# executable. build_fatpart loads it alongside freedoom's WAD at image
# build time.
#
# Restricted to the x86 family. Tilck only *runs* fbDOOM on i386 today,
# but x86_64 is kept installable so the cross-compile path stays
# exercised (we can't run any userland on x86_64 yet either, so
# "supports run" isn't the gating criterion).
#
FBDOOM_URL = GITHUB + '/maximevince/fbDOOM'

FBDOOM_SOURCE = SourceRef.new(
  name: 'fbdoom',
  url:  FBDOOM_URL,
  # Upstream has no release tags; always clone HEAD.
  git_tag: ->(_ver) { nil },
)

class FbDoomPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'fbdoom',
      source: FBDOOM_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: X86_ARCHS.values,
      dep_list: [Dep('freedoom', false)]
    )
  end

  def expected_files(ver = nil) = [
    ["fbdoom.gz", false],
  ]

  def build_steps(ver = default_ver)

    arch_tc = default_arch().gcc_tc

    return [
      Within(dir: "fbdoom", env: { "LDFLAGS" => "-static" }, steps: [
        Run(log: "build.log", argv: ["make", "NOSDL=1", "-j$PAR"]),
        Run(argv: ["#{arch_tc}-linux-strip", "--strip-all", "fbdoom"]),
        Run(argv: ["gzip", "-f", "fbdoom"]),
      ]),

      # The package's deliverable is a single fbdoom.gz binary. Move
      # it out of the fbdoom/ source subdir, then discard everything
      # else so the install tree stays small and matches
      # expected_files.
      #
      # The log is kept, which the hand-written loop this replaces did
      # not do: it lived inside fbdoom/ and went with the source. A
      # build log costs a few KB and is the only account of how the
      # binary came to be.
      Move(from: "fbdoom/fbdoom.gz", to: "fbdoom.gz"),
      Prune(keep: ["fbdoom.gz", "*.log"]),
    ]
  end
end

pkgmgr.register(FbDoomPackage.new())
