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
  CURR_TAG = pkgmgr.get_config_ver(PROJ_NAME._.upcase).to_s
  RELEASE_URL = make_gh_rel_download("vvaltchev", PROJ_NAME, CURR_TAG)
  VER_MUSL = pkgmgr.get_config_ver("MUSL")

  attr_reader :target_arch, :libc

  def initialize(target_arch, libc)
    @target_arch = target_arch
    @libc = libc
    super(
      name: "gcc_#{target_arch.name}_#{libc}",
      on_host: true,
      is_compiler: true,
      arch_list: ALL_HOST_ARCHS,
      dep_list: []
    )
  end

  def get_install_list

    def l(s) = s.length
    def drop_prefix(s, prefix) = s[l(prefix)..]
    def drop_suffix(s, suffix) = s[.. -(l(suffix) + 1)]

    list = []
    prefix = "gcc_"
    suffix = "_#{@libc}"
    target_suffix = "_#{@target_arch.name}"

    # The GCC entries look like this:
    #
    #   gcc_13_3_0_riscv64_musl
    #
    for e in HOST_ARCH_DIR_SYS.each_entry do

      e = e.to_s
      next if !e.start_with? prefix
      e = drop_prefix(e, prefix)                  # drop the "gcc_" prefix

      next if !e.end_with? suffix
      e = drop_suffix(e, suffix)                  # drop the "_musl" suffix

      next if !e.end_with? target_suffix
      e = drop_suffix(e, target_suffix)           # drop the _$ARCH suffix

      list.append(InstallInfo.new(nil, true, HOST_ARCH, Ver(e)))
    end

    return list
  end

  def installed?(ver) = get_install_list().any? { |x| x.ver == ver }

  def install_impl(ver = nil)

    ver ||= @target_arch.gcc_ver
    puts "INFO: install #{name} version: #{ver}"

    if installed? ver
      puts "INFO: already installed, skip"
      return
    end

    tarname = get_tarname(ver)
    success = Cache::download_file(RELEASE_URL, tarname)
    raise "Couldn't download file" if !success

    chdir(HOST_ARCH_DIR_SYS) do
      Cache::extract_file(tarname)
      gcc_dir = mkpathname(get_gcc_dir(ver))
      gcc_bin_dir = gcc_dir / "bin"

      raise "GCC dir #{gcc_dir} not found!" if !exist? gcc_dir
      raise "GCC dir #{gcc_bin_dir} not found!" if !exist? gcc_bin_dir

      chdir(gcc_bin_dir) do
        Dir.children(".").each(&method(:fix_single_file_name))
      end
    end
  end

  private
  def get_tarname(ver)
    archname = @target_arch.name
    host_an = HOST_ARCH.name
    os_suffix = (OS == "FreeBSD" ? "-freebsd" : "")
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
  PackageManager.instance.register(
    GccCompiler.new(arch, "musl")
  )
end


