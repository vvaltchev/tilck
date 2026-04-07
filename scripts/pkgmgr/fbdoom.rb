# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# fbDOOM — framebuffer port of DOOM, plus the freedoom WAD file. The Ruby
# port mirrors the bash script's two-tarball approach: the fbDOOM source
# (cloned from upstream) and the freedoom-VER.zip release are cached
# separately, then laid out side-by-side under the install directory so
# the FAT image build (build_fatpart) can pick up both:
#
#     <install>/fbdoom/fbdoom.gz
#     <install>/freedoom/freedoom1.wad.gz
#
# Restricted to i386: the build itself cross-compiles cleanly for any
# musl target, but Tilck only runs fbdoom on i386 today, so there's no
# point in producing artifacts for other archs.
#
class FbDoomPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  FBDOOM_URL        = GITHUB + '/maximevince/fbDOOM'
  FREEDOOM_URL_BASE = GITHUB + '/freedoom/freedoom/releases/download'

  def initialize
    super(
      name: 'fbdoom',
      url: FBDOOM_URL,
      on_host: false,
      is_compiler: false,
      arch_list: { "i386" => ALL_ARCHS["i386"] },
      dep_list: []
    )
  end

  def expected_files = [
    ["fbdoom/fbdoom.gz",          false],
    ["freedoom/freedoom1.wad.gz", false],
  ]

  def freedoom_zip(ver) = "freedoom-#{ver}.zip"

  def install_impl(ver)

    info "Install #{name} version: #{ver}"

    if installed? ver
      info "Package already installed, skip"
      return nil
    end

    # The bash script doesn't pin a fbDOOM commit — it always clones HEAD
    # and uses VER_FBDOOM purely as a directory name and as the freedoom
    # release tag. Mirror that here.
    ok = Cache::download_git_repo(
      FBDOOM_URL, tarname(ver), nil, ver_dirname(ver)
    )
    return false if !ok

    ok = Cache::download_file(
      "#{FREEDOOM_URL_BASE}/v#{ver}", freedoom_zip(ver)
    )
    return false if !ok

    pkgmgr.with_cc() do |arch_dir|
      chdir_package_base_dir(arch_dir) do
        ok = Cache::extract_file(tarname(ver), ver_dirname(ver))
        return false if !ok
        ok = chdir_install_dir(arch_dir, ver) do
          d = mkpathname(getwd)
          ok = install_impl_internal(d, ver)
          ok = check_install_dir(d, true) if ok
        end
      end
    end
    return ok
  end

  def install_impl_internal(install_dir, ver = nil)

    ver ||= default_ver
    arch_tc = default_arch().gcc_tc

    patch_fbdoom_sources
    extract_freedoom(ver)
    return build_fbdoom(arch_tc)
  end

  private

  #
  # Tilck doesn't have a writable /mnt at runtime, so redirect fbdoom's
  # config home and WAD search path to /tmp.
  #
  def patch_fbdoom_sources
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

  def extract_freedoom(ver)
    zip = (TC_CACHE / freedoom_zip(ver)).to_s
    ok = system("unzip", "-q", zip)
    raise LocalError, "fbdoom: unzip failed for #{zip}" if !ok

    extracted = "freedoom-#{ver}"
    raise LocalError, "fbdoom: missing #{extracted}/" if !File.directory?(extracted)
    mv(extracted, "freedoom")

    chdir("freedoom") do
      raise LocalError, "fbdoom: freedoom1.wad missing" if !File.file?("freedoom1.wad")
      ok = system("gzip", "-f", "freedoom1.wad")
      raise LocalError, "fbdoom: gzip freedoom1.wad failed" if !ok
    end
  end

  def build_fbdoom(arch_tc)
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
    return ok
  end
end

pkgmgr.register(FbDoomPackage.new())
