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
      arch_list: X86_ARCHS,
      dep_list: [Dep('freedoom', false)]
    )
  end

  def expected_files = [
    ["fbdoom.gz", false],
  ]

  def install_impl_internal(install_dir)

    arch_tc = default_arch().gcc_tc

    patch_sources

    ok = false
    with_saved_env(["LDFLAGS"]) do
      ENV["LDFLAGS"] = "-static"
      chdir("fbdoom") do
        ok = run_command("build.log", [
          "make", "NOSDL=1", "-j#{BUILD_PAR}",
        ])
        next if !ok

        ok = system("#{arch_tc}-linux-strip", "--strip-all", "fbdoom")
        next if !ok
        ok = system("gzip", "-f", "fbdoom")
      end
    end
    return false if !ok

    # The package's deliverable is a single fbdoom.gz binary. Move it
    # out of the fbdoom/ source subdir, then discard everything else
    # so the install tree stays small and matches expected_files.
    mv("fbdoom/fbdoom.gz", "fbdoom.gz")
    Dir.children(".").each { |e|
      next if e == "fbdoom.gz"
      rm_rf(e)
    }
    return true
  end

  private

  #
  # Tilck doesn't have a writable /mnt at runtime, so redirect fbdoom's
  # config home and WAD search path to /tmp.
  #
  def patch_sources
    chdir("fbdoom") do
      mc = "m_config.c"
      if File.exist?(mc)
        data = File.read(mc)
        if data.include?('homedir = "/mnt"')
          data.gsub!('homedir = "/mnt"', 'homedir = "/tmp"')
          File.write(mc, data)
        else
          warning "fbdoom: homedir hack not found in #{mc}"
        end
      else
        raise LocalError, "fbdoom: missing #{mc}"
      end

      ch = "config.h"
      if File.exist?(ch)
        data = File.read(ch)
        if data =~ /FILES_DIR .+/
          data.gsub!(/FILES_DIR .+/, 'FILES_DIR "/tmp"')
          File.write(ch, data)
        else
          raise LocalError, "fbdoom: FILES_DIR define not found in #{ch}"
        end
      else
        raise LocalError, "fbdoom: missing #{ch}"
      end
    end
  end
end

pkgmgr.register(FbDoomPackage.new())
