# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'package_manager'

PackageDep = Struct.new(

  "PackageDep",

  :name,        # package name (string)
  :host,        # bool: runs on host or on the target?
  :ver          # Version: can be nil, meaning "default"
)

def Dep(name, host, ver = nil)
  return PackageDep.new(name, host, ver)
end

class InstallInfo

  attr_reader :pkgname, :compiler, :on_host, :arch, :ver, :path
  attr_reader :pkg, :broken, :target_arch, :libc

  def initialize(
    pkgname,  # package name (string)
    compiler, # compiler ver used to build it, or "syscc" or nil for noarch
    on_host,  # runs on the host?
    arch,     # arch. of the installation (e.g. HOST_ARCH for compilers)
    ver,      # package version (Version object)
    path,     # installation path (directory)
    pkg = nil,# Package object or nil.
    broken      = nil, # is the package broken?
    target_arch = nil, # target architecture [only for compilers]
    libc        = nil  # libc (e.g. "musl") [only for compilers]
  )
    @pkgname = pkgname         # package name
    @compiler = compiler       # "syscc" or compiler version or nil (= noarch)
    @on_host = on_host         # runs on host_$arch or on $arch (=Tilck) ?
    @arch = arch               # arch object or nil (= noarch)
    @ver = ver                 # package version
    @path = path               # install path
    @pkg = pkg                 # package object
    @broken = broken           # broken attribute
    @target_arch = target_arch
    @libc = libc
    assert { arch.nil? or arch.is_a? Architecture }
    freeze
  end

  def compiler? = !@target_arch.nil?

  def to_s = ("I{ " +
      "pkg: #{@pkgname.ljust(20)}, comp: #{@compiler.to_s.ljust(6)}, " +
      "arch: #{((@on_host?'host_':'')+@arch.to_s).ljust(11)}, " +
      "ver: #{@ver.to_s.ljust(25)}, target: #{@target_arch.to_s.ljust(7)}, " +
      "libc: #{@libc.to_s.ljust(5)}, " +
      "path: #{@path.sub(TC.to_s + '/', '')}" +
  " }")

end

class Package

  attr_reader :name, :url, :on_host, :is_compiler, :arch_list, :dep_list

  STATUS_LEN    = 9
  INSTALLED_STR = Term.makeGreen("installed".center(STATUS_LEN))
  FOUND_STR     = Term.makeBlue("found".center(STATUS_LEN))
  SKIPPED_STR   = Term.makeYellow("skipped".center(STATUS_LEN))
  BROKEN_STR    = Term.makeRed("broken".center(STATUS_LEN))
  EMPTY_STR     = "".center(STATUS_LEN)

  public
  def initialize(name:,
                 url: nil,
                 on_host: false,
                 is_compiler: false,
                 arch_list: ALL_ARCHS,
                 dep_list: [])
    @name = name
    @url = url
    @on_host = on_host
    @is_compiler = is_compiler
    @arch_list = arch_list
    @dep_list = dep_list

    assert {
      !!on_host == !!(name.start_with? "host_" or name.start_with? "gcc_")
    }
  end

  def id = @name
  def ==(other) = (other.is_a?(Package) ? id == other.id : super)
  def eql?(other) = (self == other)
  def hash = (id.hash)

  def chdir_package_base_dir(arch_dir, &block)
    FileUtils.mkdir_p(arch_dir / pkg_dirname)
    return FileUtils.chdir(arch_dir / pkg_dirname, &block)
  end

  def chdir_install_dir(arch_dir, ver, &block)

    d = arch_dir / pkg_dirname / ver_dirname(ver)

    if !d.directory?
      error "Expected directory not found: #{d}"
      return false
    end

    return FileUtils.chdir(d, &block)
  end

  # Default implementations
  def get_install_list
    assert { !is_compiler }

    if on_host
      return syscc_package_get_install_list()
    else
      if !arch_list.nil?
        return regular_target_package_get_install_list()
      else
        return noarch_package_get_install_list()
      end
    end
  end

  def get_installable_list
    assert { !is_compiler }

    if on_host
      syscc_package_get_installable_list()
    else
      if !arch_list.nil?
        return regular_target_package_get_installable_list()
      else
        return noarch_package_get_installable_list()
      end
    end
  end

  def default_arch = ARCH
  def default_cc = ARCH.gcc_ver
  def default_ver = pkgmgr.get_config_ver(@name.sub("host_", ""))
  def tarname(ver) = "#{name}-#{ver}.tgz"
  def pkg_dirname = name.sub("host_", "")
  def ver_dirname(ver) = ver.to_s()
  def git_tag(ver) = ver.to_s()

  # Apply patch files from scripts/patches/<pkg>/<ver>/.
  # Applies common patches (*.diff in the version directory) first, then
  # arch-specific patches from a <arch>/ subdirectory, all in sorted order.
  # Called from install_impl after extraction, with cwd = source directory.
  def apply_patches(ver)

    patch_base = MAIN_DIR / "scripts" / "patches" / pkg_dirname / ver.to_s

    return if !patch_base.directory?

    arch_name = default_arch&.name

    # Collect common patches (files directly in the version directory)
    common = Pathname.glob(patch_base / "*.diff").sort

    # Collect arch-specific patches
    arch_specific = []
    if arch_name
      arch_dir = patch_base / arch_name
      if arch_dir.directory?
        arch_specific = Pathname.glob(arch_dir / "*.diff").sort
      end
    end

    patches = common + arch_specific
    return if patches.empty?

    for p in patches
      rel = p.relative_path_from(patch_base)
      info "Applying patch: #{rel}"
      ok = system("patch", "-p1", "-s", in: p.to_s)
      if !ok
        error "Failed to apply patch: #{rel}"
        return false
      end
    end
    return true
  end

  def install_impl(ver)

    info "Install #{name} version: #{ver}"

    if installed? ver
      info "Package already installed, skip"
      return nil
    end

    if !url
      raise NotImplementedError
    end

    if url.include? GITHUB
      ok = Cache::download_git_repo(url, tarname(ver), git_tag(ver))
    else
      ok = Cache::download_file(url, tarname(ver))
    end
    return false if !ok

    if on_host

      # syscc package, running on the host
      assert { default_cc.eql? "syscc" }

      chdir_package_base_dir(HOST_ARCH_DIR_SYS) do
        ok = Cache::extract_file(tarname(ver), ver_dirname(ver))
        return false if !ok
        ok = chdir_install_dir(HOST_ARCH_DIR_SYS, ver) do
          d = mkpathname(getwd)
          ok = apply_patches(ver)
          return false if ok == false
          ok = install_impl_internal(d)
          ok = check_install_dir(d, true) if ok
        end
      end

    elsif default_arch.nil?

      # noarch/source package: does not require compilation at all (lcov)
      # or does not require compilation by the toolchain (e.g. acpica).
      assert { !on_host }

      chdir_package_base_dir(TC_NOARCH) do
        ok = Cache::extract_file(tarname(ver), ver_dirname(ver))
        return false if !ok

        ok = chdir_install_dir(TC_NOARCH, ver) do
          d = mkpathname(getwd)
          ok = apply_patches(ver)
          return false if ok == false
          ok = install_impl_internal(d)
          ok = check_install_dir(d, true) if ok
        end
      end

    else

      # regular package (target = tilck architecture)
      assert { !on_host }

      pkgmgr.with_cc() do |arch_dir|
        chdir_package_base_dir(arch_dir) do

          ok = Cache::extract_file(tarname(ver), ver_dirname(ver))
          return false if !ok

          ok = chdir_install_dir(arch_dir, ver) do
            d = mkpathname(getwd)
            ok = apply_patches(ver)
            return false if ok == false
            ok = install_impl_internal(d)
            ok = check_install_dir(d, true) if ok
          end
        end
      end
    end

    return ok
  end

  def check_install_dir(d, report_error = false)
    for entry, isdir in expected_files()
      path = d / entry
      if isdir
        if !path.directory?
          error "Directory not found: #{path}" if report_error
          return false
        end
      else
        if !path.file?
          error "File not found: #{path}" if report_error
          return false
        end
      end
    end
    return true
  end

  def installed?(ver) = get_install_list().any? {
    |x| x.ver == ver and x.compiler == default_cc and x.arch == default_arch
  }

  # Methods not implemented in the base class
  def install_impl_internal(install_dir) = raise NotImplementedError
  def expected_files = raise NotImplementedError

  private
  # Generic methods used depending on the package type.

  def syscc_package_get_install_list

    list = []
    dir = HOST_ARCH_DIR_SYS / pkg_dirname

    if dir.directory?
      for d in Dir.children(dir)
        list << InstallInfo.new(
          name,                          # package name
          "syscc",                       # compiler used
          true,                          # runnning on host?
          HOST_ARCH,                     # arch
          Ver(d.to_s),                   # package version
          dir / d,                       # install path
          self,                          # package object
          !check_install_dir(dir / d)    # broken?
        )
      end
    end

    return list
  end

  def regular_target_package_get_install_list

    list = []

    for cc in Dir.children(TC)
      cc_ver = SafeVer(cc)&.to_dot
      next if ["cache", "noarch", "syscc"].include? cc
      next if !cc_ver

      for arch in Dir.children(TC / cc)
        arch_obj = ALL_ARCHS[arch.sub("host_", "")]
        dir = TC / cc / arch / pkg_dirname
        next if arch.start_with? "host_"
        next if !arch_obj
        next if !dir.directory?

        for d in Dir.children(dir) do
          list << InstallInfo.new(
            name,                        # package name
            cc_ver,                      # compiler used
            on_host,                     # runnning on host?
            arch_obj,                    # arch
            Ver(d.to_s),                 # package version
            dir / d,                     # install path
            self,                        # package object
            !check_install_dir(dir / d)  # broken?
          )
        end # for ver_dir
      end # for arch
    end # for cc
    return list
  end

  def noarch_package_get_install_list

    list = []
    dir = TC_NOARCH / pkg_dirname

    if dir.directory?
      for d in Dir.children(dir) do
        list << InstallInfo.new(
          name,
          nil,                           # compiler ver
          false,                         # on host
          nil,                           # arch
          Ver(d.to_s),                   # version
          dir / d,                       # install path
          self,                          # package object
          !check_install_dir(dir / d)    # broken?
        )
      end
    end
    return list
  end

  def syscc_package_get_installable_list = [
    InstallInfo.new(
      name,
      default_cc,
      on_host,
      default_arch,
      default_ver,
      nil,                     # install path
      self                     # package object
    )
  ]

  def regular_target_package_get_installable_list
    a = default_arch
    return [] if a.nil? || !arch_list.include?(a.name)
    [
      InstallInfo.new(
        name,
        default_cc,
        on_host,
        a,
        default_ver,
        nil,                     # install path
        self                     # package object
      )
    ]
  end

  def noarch_package_get_installable_list = [
    InstallInfo.new(
      name,
      nil,                     # compiler ver
      false,                   # on_host
      nil,                     # arch
      default_ver,
      nil,                     # install path
      self                     # package object
    )
  ]
end


