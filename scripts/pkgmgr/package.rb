# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'source_ref'
require_relative 'package_manager'
require_relative 'build_env'

PackageDep = Struct.new(

  "PackageDep",

  :name,        # package name (string)
  :host,        # bool: runs on host or on the target?
  :ver          # Version: nil means "whatever the default is"
)

# Declare a dependency. `ver` pins it to an exact version; leaving it
# nil — the normal case — means "the default version", so a coherent
# build set can live in the version files instead of being repeated at
# every edge. Only host packages can be pinned: Tilck itself is built
# from exactly one version of each package.
def Dep(name, host, ver: nil)
  return PackageDep.new(name, host, ver)
end

#
# How an installation's version was chosen, recorded in a hidden file
# inside its version directory at install time.
#
# Nothing else can tell the two cases apart: on disk a default install
# and one the user named are both just <pkg>/<ver>/, and the version
# alone cannot say whether it was asked for or merely current at the
# time. --upgrade needs the difference — a version somebody pinned must
# not be replaced behind their back.
#
# Installations made before this file existed carry none, and are read
# as :default. That is what they were: naming a version at install time
# is newer than they are, so nothing pinned can predate the file, and
# reading them any other way would quietly stop --upgrade from ever
# touching an existing toolchain.
#
module InstallOrigin

  FILE    = ".install_origin"
  DEFAULT = "default"
  PINNED  = "pinned"

  module_function

  def write(dir, default_install)
    File.write(dir / FILE, (default_install ? DEFAULT : PINNED) + "\n")
  end

  def default_install?(dir)
    path = dir / FILE
    return true if !path.file?
    return path.read.strip != PINNED
  end
end

class InstallInfo

  attr_reader :pkgname, :compiler, :on_host, :arch, :ver, :path
  attr_reader :pkg, :broken, :target_arch, :libc, :default_install

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
    libc        = nil, # libc (e.g. "musl") [only for compilers]
    default_install: false # installed as the default version?
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
    @default_install = default_install
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
    check_dep_pins(dep_list)
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

  # Is this package available at all in the current invocation?
  #
  # A package that is not enabled is not offered — it is absent from
  # `-l` and refused by `-s` — as opposed to one that merely isn't in
  # the default set. Used by the hermetic stack, which costs tens of
  # minutes to build and so must be asked for rather than stumbled
  # into.
  #
  # This gates what is OFFERED, not what exists: an installation that
  # is already on disk still shows up in `-l` whether or not the
  # package is currently enabled. Hiding it would make it unremovable
  # by name and turn a deliberate install into invisible state, which
  # is the worse failure of the two.
  def enabled? = true

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
      # Packages go in a subdirectory of their own: the install
      # scanners treat every child of this root as a package name, and
      # the composed sysroot lives beside it. Without the split, the
      # sysroot is scanned as a package called "sysroot" whose version
      # is "usr".
      when :hermetic then hermetic_root / "pkgs"
    end
  end

  # The hermetic stack lives under the version of OUR compiler, not the
  # system one: a GCC bump can change the C++ ABI, so everything built
  # with it is rebuilt beside the old stack rather than in place. Same
  # reasoning as the tier-3 <host-cc> directory, with our compiler.
  #
  # host_gcc itself lands in here too, at
  # hermetic/<gcc-ver>/gcc/<gcc-ver>/ — redundant-looking, but it keeps
  # every package on the uniform <root>/<pkg>/<ver>/ layout the install
  # scanners expect.
  # Which hermetic stack an install of `ver` belongs to.
  #
  # For everything except the compiler this is the stack the world is
  # currently being built for: choosing a different compiler means
  # changing HOST_VER_GCC and rebuilding, which is coherent. host_gcc
  # overrides it, because a compiler must belong to ITS OWN stack —
  # binding one to another compiler's stack is how a gcc ends up
  # configured against a sysroot it has no business in.
  def stack_gcc_ver(ver = nil) = pkgmgr.default_hermetic_gcc_ver

  # "gcc-<ver>", not a bare version: the compiler is named because it
  # might not always be gcc, and it matches the target-side gcc-<ver>/
  # directories at the root of the toolchain. Both live on the package
  # manager so there is a single definition.
  def hermetic_root = pkgmgr.hermetic_root(stack_gcc_ver)

  # The composed sysroot everything hermetic is built against: our
  # glibc, our headers, and a symlink farm of the resolved libraries.
  def hermetic_sysroot = pkgmgr.hermetic_sysroot(stack_gcc_ver)

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

  # Where this package's install/ directory will be AFTER the atomic
  # move out of staging.
  #
  # Most packages never need this: they are consumed through explicit
  # -I/-L, so nothing cares which path was current at build time. A
  # package that bakes an absolute --prefix into what it produces does
  # care — binutils records its ldscripts and library search paths, GCC
  # its spec files and libexec — and baking the staging path leaves it
  # pointing at a directory that ceases to exist the moment the install
  # completes.
  #
  # The version is taken from the staging directory's own name, which
  # is `ver_dirname(ver)`: staging and final share the <pkg>/<ver>/
  # tail, so this stays correct for a pinned install too.
  def final_install_prefix(staging_path)
    ver_dir = File.basename(staging_path.to_s)
    return final_install_root / pkg_dirname / ver_dir / "install"
  end

  # The version currently being installed, which is NOT default_ver
  # when the user asked for another one. Taken from the staging
  # directory's own name, the same way final_install_prefix does.
  def installing_ver(staging_path)
    return Ver(File.basename(staging_path.to_s))
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

  # The dependencies this package has when built at `ver`. A package
  # whose non-default versions need different dependency versions
  # overrides this and returns a list with those pinned; everything
  # else has one list for every version.
  #
  # Only the differences need pinning: anything left unpinned resolves
  # to its default, so a non-default build does not have to restate the
  # whole set.
  def dep_list_for(ver = nil) = dep_list

  # The environment a hermetic package builds in.
  #
  # Everything above the toolchain has to be compiled by OUR compiler,
  # or it is not hermetic regardless of what the sysroot contains: the
  # system gcc would link the system libc, bake the system interpreter
  # and emit no RPATH.
  #
  # CC and CXX are set explicitly rather than left to PATH order, since
  # a build that hardcodes "gcc" would otherwise pick up the system one
  # while everything looked correctly configured.
  #
  # PKG_CONFIG_LIBDIR, not PKG_CONFIG_PATH: the former REPLACES
  # pkg-config's default search path, the latter only prepends to it.
  # Prepending leaves /usr/lib/pkgconfig reachable, so a library
  # missing from our sysroot would be silently satisfied by the
  # system's .pc file and the build would look like it worked.
  #
  # Yields with the environment applied and restores it afterwards.
  def with_hermetic_toolchain(&block)

    gcc = pkgmgr.get("host_gcc")
    gcc_inst = gcc&.find_install(gcc.default_ver)

    bu = pkgmgr.get("host_binutils")
    bu_inst = bu&.find_install(bu.default_ver)

    if gcc_inst.nil? || bu_inst.nil?
      raise "#{name}: the hermetic toolchain is not installed; " \
            "host_gcc and host_binutils must be built first"
    end

    gcc_bin = gcc_inst.path / "install" / "bin"
    bu_bin = bu_inst.path / "install" / "bin"

    # What the dependencies publish comes first, so a build tool a
    # dependency ships is reachable by name: meson looks for ninja on
    # PATH and will not be told about it any other way.
    deps = deps_build_env.env

    vars = deps.merge({
      "CC"                => "#{gcc_bin}/gcc",
      "CXX"               => "#{gcc_bin}/g++",
      "AR"                => "#{bu_bin}/ar",
      "RANLIB"            => "#{bu_bin}/ranlib",
      "STRIP"             => "#{bu_bin}/strip",
      "PKG_CONFIG_LIBDIR" => "#{hermetic_sysroot}/usr/lib/pkgconfig",
    })

    # Our compiler ahead of everything, including any bin dir a
    # dependency contributed: nothing may shadow it.
    vars["PATH"] = [gcc_bin, bu_bin, deps["PATH"] || ENV["PATH"]].join(":")

    return with_saved_env(vars.keys) do
      vars.each { |k, v| ENV[k] = v }
      block.call
    end
  end

  # Discard the build tree, keeping the install prefix and the logs.
  #
  # The logs are the record of HOW a package was built, and they are
  # worth more than the space: warnings a newer compiler raises on older
  # code often mark undefined behaviour it is about to exploit, and that
  # signal is only visible in the build log. Deleting them alongside the
  # source made the question unanswerable without a full rebuild.
  def prune_build_tree

    # A package configured out of tree — binutils, glibc, gcc, qemu —
    # writes its logs inside the build directory about to be deleted.
    # Lift them out first, prefixed with where they came from.
    Dir.children(".").each do |d|
      next if d == "install" || !File.directory?(d)

      Dir.glob("#{d}/*.log").each do |log|
        FileUtils.mv(log, "#{d}-#{File.basename(log)}")
      end
    end

    Dir.children(".").each { |e|
      next if e == "install"
      next if e.end_with?(".log")
      rm_rf(e)
    }
  end

  # The shape every hermetic library build has.
  #
  # Our compiler; --prefix naming the SYSROOT rather than the package's
  # own directory, so the absolute paths baked into the result are the
  # ones the symlink farm makes true; the install staged through
  # DESTDIR so the tree handed to the atomic move is complete; the
  # fragment lifted into place; the source discarded.
  #
  # Only the configure invocation differs between build systems, which
  # is what the two wrappers below supply. `block` is called with the
  # prefix and the destdir and returns true on success.
  def hermetic_install(install_dir, &block)

    sysroot_usr = "#{hermetic_sysroot}/usr"
    destdir = "#{install_dir}/destdir"
    ok = false

    with_hermetic_toolchain { ok = block.call(sysroot_usr, destdir) }
    return false if !ok

    FileUtils.mkdir_p("#{install_dir}/install")
    FileUtils.mv("#{destdir}#{sysroot_usr}", "#{install_dir}/install/usr")

    prune_build_tree
    return true
  end

  # ./configure && make && make install, the shape most of the X11 and
  # freetype side of the QEMU closure uses.
  def autotools_hermetic_build(install_dir, args: [])

    return hermetic_install(install_dir) do |prefix, destdir|
      run_command("configure.log",
                  ["./configure", "--prefix=#{prefix}", *args]) &&
      run_command("build.log", ["make", "-j#{BUILD_PAR}"]) &&
      run_command("install.log", ["make", "install", "DESTDIR=#{destdir}"])
    end
  end

  # meson + ninja, the shape glib and most of the GTK stack uses.
  #
  # --libdir=lib because the sysroot has exactly one library directory;
  # meson would otherwise pick lib64 on this host and split it.
  # meson and ninja are invoked by name: they are on PATH because they
  # publish their bin dirs and with_hermetic_toolchain applies what the
  # dependencies say.
  def meson_hermetic_build(install_dir, args: [])

    return hermetic_install(install_dir) do |prefix, destdir|
      run_command("configure.log",
                  ["meson", "setup", "build",
                   "--prefix=#{prefix}", "--libdir=lib",
                   "--buildtype=release", *args]) &&
      run_command("build.log", ["ninja", "-C", "build"]) &&
      run_command("install.log",
                  ["meson", "install", "-C", "build",
                   "--destdir=#{destdir}"])
    end
  end

  # A check the package runs AFTER the sysroot has been composed.
  #
  # Some things cannot be verified at install time because they depend
  # on the sysroot including this package's own contribution, which by
  # definition has not been composed yet: gcc can prove it produces a
  # hermetic C binary before composition, but not a C++ one, since
  # libstdc++ reaches the sysroot only through the graft that follows.
  #
  # Returns true when there is nothing to check.
  def post_sysroot_check(gcc_ver = nil) = true

  # Should the hermeticity audit ask its question with a hostile
  # LD_LIBRARY_PATH?
  #
  # Yes for everything built with our toolchain: those binaries carry
  # an RPATH and must resolve correctly no matter what the environment
  # says. glibc is the exception, and the only one expected — the
  # library its own utilities need IS glibc, upstream does not rpath
  # them, and there is nothing for an RPATH to point at but the
  # loader's own home. Such a package is still audited, just without
  # the environment competing.
  def hermeticity_hostile_check? = true

  # What this package contributes to the composed sysroot.
  #
  # A hermetic package contributes its whole install tree, which is
  # sysroot-shaped by convention. Everything else contributes nothing
  # unless it overrides: host_gcc is a :distro package, but its TARGET
  # runtime — libstdc++, libgcc_s — is compiled against our glibc and
  # has to be in the sysroot for anything it builds to run.
  def sysroot_fragments(gcc_ver = nil)

    return [] if host_tier != :hermetic

    inst = find_install(default_ver)
    return [] if inst.nil?

    # Only if this install actually lives in the stack being composed.
    # Without the check, composing stack A would link in packages
    # installed under stack B.
    root = pkgmgr.hermetic_root(gcc_ver).to_s
    return [] if !inst.path.to_s.start_with?(root + "/")

    return [inst.path / "install"]
  end

  # A pin on a target dependency is meaningless — the target side is
  # one version per package by construction — so reject it rather than
  # ignore it.
  def check_dep_pins(list)

    for d in list
      next if d.ver.nil? || d.host
      raise "#{name}: dependency '#{d.name}' is pinned to #{d.ver}, but " \
            "only host packages can be pinned: Tilck is built from " \
            "exactly one version of each package"
    end

    return list
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
    return [] if !enabled?
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
  def default_ver = pkgmgr.get_config_ver(pkg_dirname, host: on_host)
  def pkg_dirname = name.sub("host_", "")
  def ver_dirname(ver) = ver.to_s()

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

        ok = check_install_dir(d, ver, true) if ok
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

    # Guard against Ruby's FileUtils.mv falling back to POSIX "mv src
    # existing_dir/" semantics, which silently nests staging INSIDE
    # final_ver_dir (→ final_ver_dir/<ver>/...) instead of replacing
    # it. We only reach this point if `installed?` returned false,
    # so if final_ver_dir is present it's either:
    #
    #   - broken (failed check_install_dir — e.g. user ran
    #     `make distclean` inside the install tree, or a partial
    #     uninstall left dangling files). Self-heal: remove it and
    #     proceed. A WARNING is emitted so the user knows the prior
    #     install was clobbered.
    #
    #   - not broken — an inconsistent state that should not normally
    #     happen (installed? should have caught it). Refuse rather
    #     than silently overwrite a valid install. The user can clear
    #     the ambiguity with `-f` (which pre-uninstalls through the
    #     main CLI flow) or `-u <pkg>`.
    if final_ver_dir.exist?
      if check_install_dir(final_ver_dir, ver)
        error "#{name}: final install dir #{final_ver_dir} already " \
              "exists and looks complete, but the package was not " \
              "detected as installed. Refusing to overwrite. " \
              "Use `-u #{name}` to remove it, or `-s #{name} -f` " \
              "to force-reinstall."
        return false
      else
        warning "#{name}: final install dir #{final_ver_dir} exists " \
                "but is broken (expected files missing). Removing " \
                "it before installing the fresh build."
        FileUtils.rm_rf(final_ver_dir)
      end
    end

    FileUtils.mv(staging.to_s, final_ver_dir.to_s)

    # Clean up the empty staging/pkg_dirname/ directory
    staging_pkg = TC_STAGING / pkg_dirname
    FileUtils.rmdir(staging_pkg) if staging_pkg.directory? &&
                                    Dir.empty?(staging_pkg)

    return true
  end

  # `ver` is passed to expected_files so a package whose install
  # layout changed across versions can return a different file list.
  # Most packages ignore it.
  def check_install_dir(d, ver, report_error = false)
    for entry, isdir in expected_files(ver)
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

  # The InstallInfo for `ver` built with this package's default compiler
  # for its default arch, or nil when that exact install is missing or
  # incomplete. Single source of truth for "which install do we mean":
  # never scan get_install_list() for "the first one that isn't broken",
  # as that picks whichever version the filesystem happens to list first.
  def find_install(ver)
    return get_install_list().find { |x|
      x.ver == ver and x.compiler == default_cc and
      x.arch == default_arch and !x.broken
    }
  end

  # A package is only "installed" if the install tree is complete (not
  # broken). Otherwise a failed earlier install (e.g. a crash after the
  # ver dir was created but before all expected files were produced)
  # would prevent `install_impl` from ever retrying on its own.
  def installed?(ver) = !find_install(ver).nil?

  # Absolute path of the install tree for `ver`. Raises when that
  # version isn't installed: dependency resolution guarantees it is, so
  # a miss is a bug to report rather than a cue to fall back on whatever
  # the host system happens to provide.
  def install_prefix(ver)

    info = find_install(ver)

    if !info
      raise "#{name} version #{ver} is not installed. " \
            "To fix: ./scripts/build_toolchain -s #{name}"
    end

    return info.path
  end

  # Was this package installed as its default version, and has that
  # default since been bumped in the version file? Only then does it
  # need upgrading.
  #
  # A version the user asked for by name is deliberate and is left
  # alone, however old it is — which is why the two cases have to be
  # distinguishable on disk at all (see InstallOrigin).
  def needs_upgrade?
    list = get_install_list.select { |x|
      x.compiler == default_cc && x.arch == default_arch && !x.broken
    }
    list.any? { |x| x.default_install && x.ver != default_ver }
  end

  # Methods not implemented in the base class
  def install_impl_internal(install_dir) = raise NotImplementedError
  def expected_files(ver = nil) = raise NotImplementedError

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

  # What this package offers to packages that depend on it, at `ver`:
  # include dirs, lib dirs, pkg-config dirs, extra environment. The base
  # class publishes nothing; a package that others link against overrides
  # this and may vary what it returns by version.
  #
  # Only the package itself knows where its headers and libraries land,
  # so this is the only place that knowledge belongs.
  def build_env(ver) = BuildEnv.empty

  # The merged build interface published by this package's dependencies,
  # each at the version bound for it, nearest dependency first.
  #
  # Consumers call this instead of naming any dependency: adding a new
  # host library to dep_list is enough for its flags to appear here.
  def deps_build_env

    versions = pkgmgr.resolved_versions(name)

    return pkgmgr.dep_closure(name).reduce(BuildEnv.empty) { |acc, dep_name|
      dep = pkgmgr.get(dep_name)
      next acc if !dep
      acc.merge(dep.build_env(versions[dep_name] || dep.default_ver))
    }
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
        ver = Ver(d.to_s)
        list << InstallInfo.new(
          name,                             # package name
          "syscc",                          # compiler used
          true,                             # runnning on host?
          HOST_ARCH,                        # arch
          ver,                              # package version
          dir / d,                          # install path
          self,                             # package object
          !check_install_dir(dir / d, ver), # broken?
          default_install: InstallOrigin.default_install?(dir / d)
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
          ver = Ver(d.to_s)
          list << InstallInfo.new(
            name,                             # package name
            cc_ver,                           # compiler used
            on_host,                          # runnning on host?
            arch_obj,                         # arch
            ver,                              # package version
            dir / d,                          # install path
            self,                             # package object
            !check_install_dir(dir / d, ver), # broken?
            default_install: InstallOrigin.default_install?(dir / d)
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
        ver = Ver(d.to_s)
        list << InstallInfo.new(
          name,
          nil,                              # compiler ver
          false,                            # on host
          nil,                              # arch
          ver,                              # version
          dir / d,                          # install path
          self,                             # package object
          !check_install_dir(dir / d, ver), # broken?
          default_install: InstallOrigin.default_install?(dir / d)
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


