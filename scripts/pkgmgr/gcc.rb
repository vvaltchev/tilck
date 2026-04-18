# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class GccCompiler < Package

  include FileShortcuts
  include FileUtilsShortcuts

  PROJ_NAME = "musl-cross-make"
  CURR_TAG = pkgmgr.get_config_ver(PROJ_NAME).to_s
  VER_MUSL = pkgmgr.get_config_ver("musl")
  ALL_VERSIONS = [Ver("12.4.0"), Ver("13.3.0")]

  attr_reader :target_arch, :libc

  def initialize(target_arch, libc)
    @target_arch = target_arch
    @libc = libc
    # Each (target_arch, libc) pair has its own pre-built tarball on
    # the musl-cross-make release page; the compiler binaries inside
    # differ by target arch, so no SourceRef sharing is possible.
    src = SourceRef.new(
      name: "gcc-#{target_arch.name}-#{libc}",
      url:  make_gh_rel_download("vvaltchev", PROJ_NAME, CURR_TAG),
      tarname: ->(ver) { self.class.build_tarname(target_arch, libc, ver) },
    )
    super(
      name: "gcc-#{target_arch.name}-#{libc}",
      source: src,
      on_host: true,
      is_compiler: true,
      host_tier: :portable,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: []
    )
  end

  def expected_files = [
    "bin/#{target_arch.gcc_tc}-linux-gcc",
    "bin/#{target_arch.gcc_tc}-linux-g++",
    "bin/#{target_arch.gcc_tc}-linux-ar",
    "bin/#{target_arch.gcc_tc}-linux-as",
    "bin/#{target_arch.gcc_tc}-linux-ld",
    "bin/#{target_arch.gcc_tc}-linux-nm",
    "bin/#{target_arch.gcc_tc}-linux-objcopy",
    "bin/#{target_arch.gcc_tc}-linux-objdump",
    "bin/#{target_arch.gcc_tc}-linux-readelf",
    "bin/#{target_arch.gcc_tc}-linux-ranlib",
    "bin/#{target_arch.gcc_tc}-linux-strip",
  ]

  # Wrap the base class install list with target_arch/libc metadata, so
  # PackageManager#get_installed_compilers can select installed cross-
  # compilers for a specific target architecture.
  def get_install_list
    super.map { |info|
      InstallInfo.new(
        info.pkgname, info.compiler, info.on_host, info.arch,
        info.ver, info.path, info.pkg, info.broken,
        @target_arch, @libc
      )
    }
  end

  def get_installable_list
    ALL_VERSIONS.map { |ver|
      InstallInfo.new(
        name, "syscc", true, HOST_ARCH, ver, nil,
        self, nil, @target_arch, @libc
      )
    }
  end

  def default_ver = @target_arch.gcc_ver
  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  # GCC compilers are default based on the current target ARCH:
  # x86 family needs both i386 and x86_64 (UEFI bootloader requires
  # x86_64); other arches need just their own compiler.
  def default?
    return false if !host_supported?
    if ARCH.family == "generic_x86"
      return @target_arch == ALL_ARCHS["i386"] ||
             @target_arch == ALL_ARCHS["x86_64"]
    end
    return @target_arch == ARCH
  end

  # Called by the SourceRef's tarname Proc: the cache filename
  # encodes target arch, libc version, gcc version, and host
  # arch/OS because the upstream release page ships a distinct
  # tarball for each combination.
  def self.build_tarname(target_arch, libc, ver)
    archname = target_arch.name
    host_an = HOST_ARCH.name

    case OS
      when "FreeBSD"
        os_suffix = "-freebsd"
      when "Darwin"
        os_suffix = "-darwin25"
      else
        os_suffix = ""
    end

    verStr = ver.to_s()
    ext = ".tar.bz2"
    "#{archname}-musl-#{VER_MUSL}-gcc-#{verStr}-#{host_an}#{os_suffix}#{ext}"
  end

  # Called by Package#install_impl from within the extracted installation
  # directory. Rename binaries like i686-linux-musl-gcc to i686-linux-gcc
  # (and fix any symlinks that point to them) to produce a canonical,
  # libc-agnostic tool name that package_manager#with_cc can use.
  def install_impl_internal(install_dir)
    chdir("bin") do
      Dir.children(".").each(&method(:fix_single_file_name))
    end
    return true
  end

  private
  def fix_single_file_name(name)

    new_name = name.sub("musl-", "")

    if file? name

      mv(name, new_name) unless new_name == name

    elsif symlink? name

      target = readlink(name)
      new_target = target.sub("musl-", "")
      if new_target != target || new_name != name
        rm_f(name)
        symlink(new_target, new_name)
      end

    end
  end
end # class GccCompiler

for name, arch in ALL_ARCHS do
  pkgmgr.register(GccCompiler.new(arch, "musl"))
end
