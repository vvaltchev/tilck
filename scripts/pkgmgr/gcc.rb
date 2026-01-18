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
  RELEASE_URL = make_gh_rel_download("vvaltchev", PROJ_NAME, CURR_TAG)
  VER_MUSL = pkgmgr.get_config_ver("musl")
  ALL_VERSIONS = [Ver("12.4.0"), Ver("13.3.0")]

  attr_reader :target_arch, :libc

  def initialize(target_arch, libc)
    @target_arch = target_arch
    @libc = libc
    super(
      name: pkgmgr.build_gcc_package_name(target_arch, libc),
      on_host: true,
      is_compiler: true,
      arch_list: ALL_HOST_ARCHS,
      dep_list: []
    )
  end

  def get_install_list
    list = []
    for e in HOST_ARCH_DIR_SYS.each_entry do
      parsed_gcc_info = pkgmgr.parse_gcc_dir(e.to_s())
      if parsed_gcc_info
        ver, target_arch, libc = parsed_gcc_info
        pkgname = pkgmgr.build_gcc_package_name(target_arch, libc)
        p = HOST_ARCH_DIR_SYS / e
        if pkgname == name
          list << InstallInfo.new(
            name, "syscc", true, HOST_ARCH, ver, p, self,
            target_arch, libc
          )
        end
      end
    end
    return list
  end

  def get_installable_list
    list = []
    for ver in ALL_VERSIONS do
      list << InstallInfo.new(
        name, "syscc", true, HOST_ARCH, ver, nil, self,
        target_arch, libc
      )
    end
    return list
  end

  def default_ver = @target_arch.gcc_ver
  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  def install_impl(ver = nil)

    ver ||= default_ver()
    info "Install #{name} version: #{ver}"

    if installed? ver
      info "Package already installed, skip"
      return true
    end

    tarname = get_tarname(ver)
    success = Cache::download_file(RELEASE_URL, tarname)
    raise LocalError, "Couldn't download file" if !success

    chdir(HOST_ARCH_DIR_SYS) do
      Cache::extract_file(tarname)
      gcc_dir = mkpathname(get_gcc_dir(ver))
      gcc_bin_dir = gcc_dir / "bin"

      raise LocalError, "GCC dir #{gcc_dir} not found!" if
        !exist? gcc_dir

      raise LocalError, "GCC dir #{gcc_bin_dir} not found!" if
        !exist? gcc_bin_dir

      chdir(gcc_bin_dir) do
        Dir.children(".").each(&method(:fix_single_file_name))
      end
    end

    return true

  rescue LocalError => e
    error e
    return false
  end

  private
  def installed?(ver) = get_install_list().any? { |x| x.ver == ver }

  def get_tarname(ver)
    archname = @target_arch.name
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

  def get_gcc_dir(ver) = "gcc_#{ver._}_#{@target_arch.name}_musl"

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

