# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class GccCompiler < Package

  PROJ_NAME = "musl-cross-make"
  CURR_TAG = pkgmgr.get_config_ver(PROJ_NAME, host: true).to_s

  # Deliberately a TARGET version read from a host package: the musl
  # libc baked into the cross-compiler is the one Tilck links against,
  # and it is part of the release tarball's name (see build_tarname).
  # libmusl reads the same entry, so the two cannot drift apart.
  VER_MUSL = pkgmgr.get_config_ver("musl", host: false)
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

  def expected_files(ver = nil) = [
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

  # Wrap the base class reading of the tree with target_arch/libc
  # metadata, so PackageManager#get_installed_compilers can select
  # installed cross-compilers for a specific target architecture.
  def read_install_list(host) = super.map { |info| annotate_install(info) }

  # An install of a cross compiler says what it targets.
  def annotate_install(info)
    return InstallInfo.new(
      info.pkgname, info.compiler, info.on_host, info.arch,
      info.ver, info.path, info.pkg, info.broken,
      @target_arch, @libc,
      default_install: info.default_install, manual: info.manual,
      coords: info.coords, record: info.record
    )
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

  # GCC compilers are default based on the target arch being built
  # for: x86 family needs both i386 and x86_64 (UEFI bootloader
  # requires x86_64); other arches need just their own compiler.
  #
  # The arch of the invocation's scope, not the global: `-a riscv64`
  # with no mode installs riscv64's defaults, and this is what says
  # which compiler that includes.
  def default?

    return false if !host_supported?

    arch = scope.arch

    if arch.family == "generic_x86"
      return @target_arch == ALL_ARCHS["i386"] ||
             @target_arch == ALL_ARCHS["x86_64"]
    end

    return @target_arch == arch
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
  # The tarball's tools are named <triple>-musl-<tool>, and the one
  # symlink in bin/ (cc -> gcc) points at a musl name too; Tilck's
  # build wants them without the "musl-". Links first, while their
  # names still match the glob and their targets still say what to
  # strip; then every entry is renamed. A link whose target is NOT a
  # musl name would fail the Transform, which is right: in this
  # tarball that would be news.
  def build_steps(ver = default_ver) = [
    Within(dir: "bin", steps: [
      ForEach(glob: "*musl*", as: "l", kind: :symlink, steps: [
        Readlink(bind: "t", path: "$l"),
        Transform(bind: "t", from: "$t", subs: [["musl-", ""]]),
        Symlink(target: "$t", link: "$l"),
      ]),
      ForEach(glob: "*musl*", as: "f", steps: [
        Transform(bind: "g", from: "$f", subs: [["musl-", ""]]),
        Move(from: "$f", to: "$g"),
      ]),
    ]),
  ]
end # class GccCompiler

for name, arch in ALL_ARCHS do
  pkgmgr.register(GccCompiler.new(arch, "musl"))
end
