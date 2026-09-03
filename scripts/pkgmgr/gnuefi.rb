# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

GNUEFI_URL = GITHUB + '/vvaltchev/gnu-efi-fork'

#
# Shared SourceRef: `gnuefi_src` (headers consumed by kernel build)
# and `gnuefi` (arch-specific built libraries) both fetch from the
# same upstream and so share a single SourceRef — the tarball is
# downloaded once and cached.
#
GNUEFI_SOURCE = SourceRef.new(
  name: 'gnuefi',
  url:  GNUEFI_URL,
)

GNUEFI_COMMON_EXPECTED_FILES = [
  ["inc", true],
  ["gnuefi", true],
  ["lib", true],
  ["Makefile", false],
]

#
# Source-only (noarch) gnuefi: just the extracted source tree.
# Used by kernel C files to include GNU-EFI headers — a legitimate
# deliverable, not an implementation detail. Needed on every arch
# including those where the built libraries aren't produced.
#
class GnuefiSourcePackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'gnuefi_src',
      source: GNUEFI_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # noarch
      dep_list: [],
      default: true,
    )
  end

  def pkg_dirname = "gnuefi"
  def default_ver = pkgmgr.get_config_ver("gnuefi", host: false)
  def expected_files(ver = nil) = GNUEFI_COMMON_EXPECTED_FILES
  def default_arch = nil
  def default_cc = nil

  def install_impl_internal(ignored = nil)
    return true
  end
end

#
# Arch-specific gnuefi: patched and compiled for x86 targets.
# Depends on gnuefi_src (shares the same tarball in cache).
#
class GnuefiPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'gnuefi',
      source: GNUEFI_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: X86_ARCHS.values,
      dep_list: [Dep('gnuefi_src', false)],
      default: true,
    )
  end

  def pkg_dirname = "gnuefi"

  def expected_files(ver = nil) = GNUEFI_COMMON_EXPECTED_FILES

  #
  # The UEFI bootloader always needs x86_64 gnuefi, even when the
  # target arch is i386. Mirror the bash script behavior: build for
  # the current arch + x86_64.
  #
  def archs_needed
    # Use default_arch (not the bare ARCH) so that -s gnuefi -a <x86>
    # still computes the pair correctly: target arch + x86_64.
    archs = [default_arch]
    x64 = ALL_ARCHS["x86_64"]
    archs << x64 if default_arch != x64
    archs
  end

  # Installed means every arch it builds for is there -- each at its
  # own coordinates, board included. Matching on the arch alone
  # accepted any board's copy for any other.
  def installed?(ver)
    list = get_install_list()
    archs_needed.all? do |arch|
      want = pkgmgr.with_target_arch(arch) { coords(ver) }
      list.any? { |x| x.ver == ver && x.coords == want && !x.broken }
    end
  end

  # One call builds every arch in archs_needed, so all of them are
  # recorded -- and only them.
  def install_archs(ver = nil) = archs_needed

  def install_impl(ver)

    info "Install #{name} version: #{ver}"

    if installed?(ver)
      info "Package already installed, skip"
      return nil
    end

    ok = @source.download(ver)
    return false if !ok

    for arch in archs_needed
      pkgmgr.with_cc(arch.name) do |arch_dir|
        chdir_package_base_dir(arch_dir) do
          ok = @source.extract(ver, ver_dirname(ver))
          return false if !ok
          ok = chdir_install_dir(arch_dir, ver) do
            d = mkpathname(getwd)

            # This package extracts the tarball once per arch and so
            # replaces the base class's install_impl wholesale --
            # which is where patches are normally applied. Apply them
            # here, per extraction, or they are silently not applied
            # at all.
            next false if !apply_patches(ver)

            ok = install_impl_internal(d, arch)
            ok = check_install_dir(d, ver, true) if ok
          end
        end
      end
      return false if !ok
    end

    return ok
  end

  def install_impl_internal(install_dir, arch = nil)

    arch ||= default_arch()

    efi = arch.efi
    tc = arch.gcc_tc

    ok = run_command("build_#{efi}.log", [
      "make",
      "ARCH=#{efi}",
      "prefix=#{tc}-linux-",
      "CROSS_COMPILE=",
      "OS=Linux",
      "-j#{BUILD_PAR}",
    ])
    return false if !ok
    return true
  end
end

pkgmgr.register(GnuefiSourcePackage.new())
pkgmgr.register(GnuefiPackage.new())
