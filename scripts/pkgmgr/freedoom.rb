# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# Freedoom — the game data blob (IWAD) needed to run fbDOOM. Upstream
# ships `freedoom-<ver>.zip` release assets under
# github.com/freedoom/freedoom/releases/download/v<ver>/<asset>.
#
# Packaged separately from fbDOOM to model a clean runtime dependency:
# the engine and the data are independent upstream projects that only
# happen to be shipped side-by-side in Tilck's FAT image. `fbdoom`
# depends on `freedoom` in the pkgmgr dep graph.
#
# Restricted to the x86 family. Tilck only *runs* fbDOOM on i386 today,
# but x86_64 stays installable so fbdoom's cross-compile path is
# exercised there (freedoom is fbdoom's runtime dep, so it must be
# available for every arch where fbdoom is installable).
#
# The SourceRef captures the cache filename but not the download URL —
# the upstream release layout interpolates a `v<ver>/` segment between
# the URL base and the asset, and Cache::download_file's remote_file
# param must be a bare filename (no slashes). install_impl drives the
# download manually and hands the cached zip to a custom unzip +
# gzip pipeline.
#
FREEDOOM_URL_BASE = GITHUB + '/freedoom/freedoom/releases/download'

FREEDOOM_SOURCE = SourceRef.new(
  name:    'freedoom',
  url:     FREEDOOM_URL_BASE,
  tarname: ->(ver) { "freedoom-#{ver}.zip" },
)

class FreedoomPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'freedoom',
      source: FREEDOOM_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: X86_ARCHS.values,
      dep_list: []
    )
  end

  def expected_files = [
    ["freedoom1.wad.gz", false],
  ]

  def install_impl(ver)

    info "Install #{name} version: #{ver}"

    if installed? ver
      info "Package already installed, skip"
      return nil
    end

    zip = @source.tarname(ver)

    # Upstream URL: <base>/v<ver>/freedoom-<ver>.zip
    ok = Cache::download_file("#{FREEDOOM_URL_BASE}/v#{ver}", zip)
    return false if !ok

    # Set up staging and extract the zip into it.
    staging = staging_dir(ver)
    FileUtils.rm_rf(staging)
    FileUtils.mkdir_p(staging)

    cached_zip = (TC_CACHE / zip).to_s
    FileUtils.chdir(staging) do
      ok = system("unzip", "-q", cached_zip)
      return false if !ok

      # The zip's top-level dir is `freedoom-<ver>/`; flatten into
      # the staging root so the WAD lands directly at
      # <staging>/freedoom1.wad.gz.
      extracted = "freedoom-#{ver}"
      if !File.directory?(extracted)
        error "#{name}: expected dir missing: #{extracted}"
        return false
      end

      Dir.children(extracted).each do |child|
        FileUtils.mv("#{extracted}/#{child}", child)
      end
      FileUtils.rmdir(extracted)

      if !File.file?("freedoom1.wad")
        error "#{name}: freedoom1.wad missing"
        return false
      end

      ok = system("gzip", "-f", "freedoom1.wad")
      return false if !ok

      # Discard everything else the zip shipped (docs, freedoom2.wad,
      # etc.) — we only use freedoom1.wad.gz and freedoom2.wad alone
      # is ~20MB of wasted disk space.
      Dir.children(".").each { |e|
        next if e == "freedoom1.wad.gz"
        FileUtils.rm_rf(e)
      }
    end

    # Promote staging -> final install dir.
    final_root = final_install_root
    final_pkg_dir = final_root / pkg_dirname
    final_ver_dir = final_pkg_dir / ver_dirname(ver)
    FileUtils.mkdir_p(final_pkg_dir)
    FileUtils.mv(staging.to_s, final_ver_dir.to_s)

    staging_pkg = TC_STAGING / pkg_dirname
    FileUtils.rmdir(staging_pkg) if staging_pkg.directory? &&
                                    Dir.empty?(staging_pkg)

    return check_install_dir(final_ver_dir, true)
  end

  # Unused but required to satisfy the Package contract.
  def install_impl_internal(install_dir) = true
end

pkgmgr.register(FreedoomPackage.new())
