# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'source_ref'
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

  attr_reader :name, :source, :on_host, :is_compiler, :arch_list, :dep_list
  attr_reader :host_tier

  STATUS_LEN    = 9
  INSTALLED_STR = Term.makeGreen("installed".center(STATUS_LEN))
  FOUND_STR     = Term.makeBlue("found".center(STATUS_LEN))
  SKIPPED_STR   = Term.makeYellow("skipped".center(STATUS_LEN))
  BROKEN_STR    = Term.makeRed("broken".center(STATUS_LEN))
  EMPTY_STR     = "".center(STATUS_LEN)

  public
  # host_tier controls where host packages are installed:
  #   :portable  — statically linked, any distro   (HOST_DIR_PORTABLE)
  #   :distro    — links distro libc, any host CC   (HOST_DIR_DISTRO)
  #   :compiler  — depends on host CC C++ ABI       (HOST_DIR)
  #
  # @param source [SourceRef, nil] where the package's source comes
  #   from. Required for packages that use the base class install
  #   flow. May be nil for packages with a custom install_impl that
  #   fetches artefacts another way (e.g. a vendor-prebuilt blob).
  def initialize(name:,
                 source: nil,
                 on_host: false,
                 is_compiler: false,
                 host_tier: :compiler,
                 arch_list: ALL_ARCHS.values,
                 dep_list: [],
                 host_os_list: nil,
                 host_arch_list: nil,
                 default: false,
                 board_list: nil)
    @name = name
    @source = source
    @on_host = on_host
    @is_compiler = is_compiler
    @host_tier = host_tier
    @arch_list = arch_list
    @dep_list = dep_list
    @host_os_list = host_os_list
    @host_arch_list = host_arch_list
    @default = default
    @board_list = board_list

    assert {
      !!on_host == !!(name.start_with?("host_") || is_compiler)
    }
    assert { source.nil? or source.is_a?(SourceRef) }
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

  # Is the current target arch supported by this package?
  # Noarch (arch_list nil) and host packages are always true.
  # Reads pkgmgr.target_arch so the answer reflects the `-a <arch>`
  # install-mode override when one is active.
  def arch_supported?
    return true if @arch_list.nil? || @on_host
    return @arch_list.include?(pkgmgr.target_arch)
  end

  # Should this package be auto-installed for the current config?
  # Subclasses (e.g. GccCompiler) can override for richer logic.
  def default?
    @default && host_supported? && board_supported? && arch_supported?
  end

  def host_install_root
    case @host_tier
      when :portable then HOST_DIR_PORTABLE
      when :distro   then HOST_DIR_DISTRO
      when :compiler then HOST_DIR
    end
  end

  # The root directory for the final install (where mv moves to).
  # For target packages, uses default_arch (which for the base class
  # is pkgmgr.target_arch) so the install lands under the right
  # gcc-<ver>/<arch>/ tree when `-a <arch>` is active.
  def final_install_root
    if on_host
      host_install_root
    elsif (a = default_arch).nil?
      TC_NOARCH
    else
      TC / "gcc-#{a.gcc_ver}" / a.name
    end
  end

  # Staging path for this package version.
  def staging_dir(ver)
    TC_STAGING / pkg_dirname / ver_dirname(ver)
  end

  # Clean build artifacts from a staging directory, keeping the
  # extracted source for a rebuild. Returns true if clean succeeded.
  # Override in subclass for custom logic. Fallback: delete + re-extract.
  def clean_build(dir)
    system("make", "distclean", chdir: dir.to_s,
           out: "/dev/null", err: "/dev/null")
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

  # Default arch / compiler for a regular target package: the pkgmgr's
  # current target_arch (ARCH unless a with_target_arch(...) override
  # is active). Host and noarch packages override both.
  def default_arch = pkgmgr.target_arch
  def default_cc = pkgmgr.target_arch.gcc_ver
  def default_ver = pkgmgr.get_config_ver(@name.sub("host_", ""))
  def pkg_dirname = name.sub("host_", "")
  def ver_dirname(ver) = ver.to_s()

  # Apply patch files from scripts/patches/<pkg>/<ver>/.
  # Applies common patches (*.diff in the version directory) first, then
  # arch-specific patches from a <arch>/ subdirectory, all in sorted order.
  # Called from install_impl after extraction, with cwd = source directory.
  # Apply patch files from scripts/patches/<pkg>/<ver>/.
  # Applies common patches (*.diff in the version directory) first, then
  # arch-specific patches from a <arch>/ subdirectory, all in sorted order.
  # Called from install_impl after extraction, with cwd = source directory.
  #
  # Returns true on success (including "no patches to apply"), false on
  # failure. Never returns nil.
  def apply_patches(ver)

    patch_base = MAIN_DIR / "scripts" / "patches" / pkg_dirname / ver.to_s
    return true if !patch_base.directory?

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
    return true if patches.empty?

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

    if !@source
      raise NotImplementedError,
            "#{name}: no source declared and no custom install_impl"
    end

    # --- Download (into cache/) ---

    ok = @source.download(ver)
    return false if !ok

    # --- Ensure extracted source in staging ---

    staging = staging_dir(ver)

    if staging.directory?
      # Recovery: staging exists from a previous interrupted build.
      # Clean build artifacts, keep extracted source for rebuild.
      info "Resuming from staging (cleaning build artifacts)"
      if !clean_build(staging)
        # clean_build failed — delete and re-extract
        warning "clean_build failed, re-extracting"
        FileUtils.rm_rf(staging)
      end
    end

    if !staging.directory?
      # Fresh extraction into staging
      chdir_package_base_dir(TC_STAGING) do
        ok = @source.extract(ver, ver_dirname(ver))
        return false if !ok
      end
    end

    # --- Build in staging (signal-safe) ---
    #
    # On SIGINT/SIGTERM/SIGHUP/SIGQUIT: clean build artifacts from
    # the staging dir (preserving extracted source for next run),
    # then exit. The final install dir is never in a partial state.

    cleanup = -> {
      $stderr.puts "\n*** Interrupted — cleaning build artifacts ***"
      clean_build(staging)
      exit 1
    }

    signals = %w[INT TERM HUP QUIT]
    old_handlers = signals.map { |sig|
      [sig, Signal.trap(sig) { cleanup.call }]
    }

    begin
      ok = chdir_install_dir(TC_STAGING, ver) do
        d = mkpathname(getwd)

        return false if !apply_patches(ver)

        if !on_host && (a = default_arch) && !a.nil?
          # Target package: need cross-compiler in PATH. Pass the
          # arch name explicitly so with_target_arch scoping is
          # respected — with_cc() with no arg defaults to ARCH
          # which might differ from target_arch.
          pkgmgr.with_cc(a.name) do |_arch_dir|
            ok = install_impl_internal(d)
          end
        else
          ok = install_impl_internal(d)
        end

        ok = check_install_dir(d, true) if ok
        ok
      end

      return false if !ok
    ensure
      # Restore original signal handlers
      old_handlers.each { |sig, handler|
        Signal.trap(sig, handler || "DEFAULT")
      }
    end

    # --- Atomic move to final location ---

    final_root = final_install_root
    final_pkg_dir = final_root / pkg_dirname
    final_ver_dir = final_pkg_dir / ver_dirname(ver)

    FileUtils.mkdir_p(final_pkg_dir)
    FileUtils.mv(staging.to_s, final_ver_dir.to_s)

    # Clean up the empty staging/pkg_dirname/ directory
    staging_pkg = TC_STAGING / pkg_dirname
    FileUtils.rmdir(staging_pkg) if staging_pkg.directory? &&
                                    Dir.empty?(staging_pkg)

    return true
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

  # Does this package have an older version installed but not the
  # current one (from pkg_versions)? If so, it needs upgrading.
  def needs_upgrade?
    list = get_install_list.select { |x|
      x.compiler == default_cc && x.arch == default_arch && !x.broken
    }
    !list.empty? && !list.any? { |x| x.ver == default_ver }
  end

  # Methods not implemented in the base class
  def install_impl_internal(install_dir) = raise NotImplementedError
  def expected_files = raise NotImplementedError

  # Normalize a kernel-style .config file: strip metadata header,
  # empty lines, non-CONFIG lines, and reverse-sort by binary value.
  # Used by busybox and u-boot for reproducible diffs.
  def fix_config_file(path = ".config")
    data = File.read(path)
    lines = data.lines()
    lines = lines[4...] # drop first 4 lines (metadata header)
    lines.select! { |x| !x.strip.blank? }
    lines.select! { |x| !x.index("CONFIG_").nil? }
    lines.map! { |x| x.rstrip }
    lines = stable_sort(lines) { |x, y| -(x.b <=> y.b) }
    File.write(path, lines.join("\n") + "\n")
  end

  # Interactive reconfiguration (e.g. `make menuconfig`). Only packages
  # that override config_impl are configurable. The base class runs the
  # override inside the installed version's directory with the cross-
  # compiler in PATH.
  def configurable? = false

  def configure(ver = nil)
    ver ||= default_ver
    if !installed?(ver)
      error "#{name} is not installed (version #{ver})"
      return false
    end

    pkgmgr.with_cc() do |arch_dir|
      chdir_install_dir(arch_dir, ver) do
        return config_impl
      end
    end
  end

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
    return [] if a.nil? || !arch_list.include?(a)
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


