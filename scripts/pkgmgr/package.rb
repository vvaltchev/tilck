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
  attr_reader :portable

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
                 portable: false,
                 arch_list: ALL_ARCHS,
                 dep_list: [],
                 host_os_list: nil,
                 host_arch_list: nil,
                 default: false,
                 board_list: nil)
    @name = name
    @url = url
    @on_host = on_host
    @is_compiler = is_compiler
    @portable = portable
    @arch_list = arch_list
    @dep_list = dep_list
    @host_os_list = host_os_list
    @host_arch_list = host_arch_list
    @default = default
    @board_list = board_list

    assert {
      !!on_host == !!(name.start_with?("host_") || is_compiler)
    }
  end

  # Can this package run / be built on the current host?
  # nil lists mean "any"; non-nil lists are allowlists.
  def host_supported?
    return false if @host_os_list && !@host_os_list.include?(HOST_OS)
    return false if @host_arch_list && !@host_arch_list.include?(HOST_ARCH.name)
    return true
  end

  # Is the current board supported? nil = any board.
  def board_supported?
    return true if @board_list.nil?
    return @board_list.include?(BOARD)
  end

  # Should this package be auto-installed for the current config?
  # Subclasses (e.g. GccCompiler) can override for richer logic.
  def default?
    @default && host_supported? && board_supported?
  end

  # The host install root for syscc packages.
  #
  # Portable host tools (statically-linked, e.g. the cross-compilers) live
  # under host/<os>-<arch>/portable/, shared across distros and host
  # compilers. Non-portable host tools live under
  # host/<os>-<arch>/<distro>/<host-cc>/ because they depend on the system
  # libraries and on the specific host compiler used to build them.
  def host_install_root
    @portable ? HOST_DIR_PORTABLE : HOST_DIR
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
    return [] if !host_supported? || !board_supported?
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

  # Filename to fetch from the remote `url`. Defaults to the same name we
  # store in the local cache (`tarname(ver)`). Override when the upstream
  # serves the archive under a different name (e.g. github tag archives,
  # which are served as `<tag>.tar.gz` regardless of the repo name).
  def remote_tarname(ver) = tarname(ver)

  # Whether to fetch the source via `git clone` (true) rather than a plain
  # HTTP download (false). Default heuristic: github.com URLs pointing at a
  # repo root — i.e. NOT at a release asset (/releases/download/) and NOT
  # at a tag/branch zip (/archive/). Packages hosted on non-github git
  # servers (e.g. git.musl-libc.org, repo.or.cz) must override this to
  # return true.
  def fetch_via_git?
    github_tarball = url.include?("/releases/download/") ||
                     url.include?("/archive/")
    return url.include?(GITHUB) && !github_tarball
  end

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

    if !host_supported?
      req = [@host_os_list, @host_arch_list].compact.map { |l|
        l.join("/")
      }.join(" ")
      error "#{name} requires a #{req} host"
      return false
    end

    if !board_supported?
      error "#{name} requires board #{@board_list.join('/')}"
      return false
    end

    info "Install #{name} version: #{ver}"

    if installed? ver
      info "Package already installed, skip"
      return nil
    end

    if !url
      raise NotImplementedError
    end

    # Delegate the git-vs-HTTP decision to `fetch_via_git?`, which has a
    # GitHub-aware default and can be overridden by packages hosted on
    # other git servers. Passing `ver_dirname(ver)` as the clone dir
    # name keeps cached tarballs laid out the same way regardless of
    # whether `git_tag(ver)` happens to differ from the version string
    # (e.g. musl tags its releases as `v1.2.5`, but we want `1.2.5/` as
    # the top-level dir inside the cached .tgz).
    if fetch_via_git?
      ok = Cache::download_git_repo(
             url, tarname(ver), git_tag(ver), ver_dirname(ver)
           )
    else
      ok = Cache::download_file(url, remote_tarname(ver), tarname(ver))
    end
    return false if !ok

    if on_host

      # syscc package, running on the host
      assert { default_cc.eql? "syscc" }

      root = host_install_root
      chdir_package_base_dir(root) do
        ok = Cache::extract_file(tarname(ver), ver_dirname(ver))
        return false if !ok
        ok = chdir_install_dir(root, ver) do
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

  # A package is only "installed" if the install tree is complete (not
  # broken). Otherwise a failed earlier install (e.g. a crash after the
  # ver dir was created but before all expected files were produced)
  # would prevent `install_impl` from ever retrying on its own.
  def installed?(ver) = get_install_list().any? { |x|
    x.ver == ver and x.compiler == default_cc and
    x.arch == default_arch and !x.broken
  }

  # Methods not implemented in the base class
  def install_impl_internal(install_dir) = raise NotImplementedError
  def expected_files = raise NotImplementedError

  private
  # Generic methods used depending on the package type.

  def syscc_package_get_install_list

    list = []
    dir = host_install_root / pkg_dirname

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

    for cc_dir in Dir.children(TC)
      next if !cc_dir.start_with?("gcc-")

      cc_ver = SafeVer(cc_dir.sub("gcc-", ""))
      next if !cc_ver

      for arch_name in Dir.children(TC / cc_dir)
        arch_obj = ALL_ARCHS[arch_name]
        next if !arch_obj

        dir = TC / cc_dir / arch_name / pkg_dirname
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


