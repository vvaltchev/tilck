# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'source_ref'
require_relative 'package_manager'
require_relative 'build_env'
require_relative 'coords'
require_relative 'source_digest'
require_relative 'build_inputs'

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
  attr_reader :coords

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
    default_install: false, # installed as the default version?
    coords: nil             # Coords: where this installation lives
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
    @coords = coords           # the three coordinates of the install
    assert { arch.nil? or arch.is_a? Architecture }

    # An actual installation always knows where it lives. A candidate
    # from get_installable_list does not: it has no path yet, and for a
    # target package its stack coordinate names a cross-compiler version
    # that is only known once one is installed. nil there is the honest
    # answer, so require the coordinates exactly where they exist.
    assert { path.nil? || coords.is_a?(Coords) }
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

  STATUS_LEN    = 9              # "installed", "not built"
  COUNT_DIGITS  = 2              # nothing here is installed 100 times
  COUNT_LEN     = COUNT_DIGITS + 3   # " (99)"
  STATUS_CELL   = STATUS_LEN + COUNT_LEN

  # One status cell: the word in its own colour, the count of matching
  # installs in none, the pair centred AS A UNIT.
  #
  # Centred as a unit rather than appended, because the field after it
  # would otherwise start at a different column for every package --
  # the count is the one part of the line whose width varies, so it
  # has to be absorbed here.
  def self.status_str(word, color, n = nil, width: STATUS_CELL)

    # Padded INSIDE the brackets, so that a two-digit count widens
    # nothing: "( 4)" and "(10)" are the same width, and the cell
    # needs no slack after them.
    tail = n.nil? ? "" : " (#{n.to_s.rjust(COUNT_DIGITS)})"
    pad  = [width - word.length - tail.length, 0].max
    lpad = pad / 2

    return "#{' ' * lpad}#{Term.send(color, word)}#{tail}#{' ' * (pad - lpad)}"
  end

  def self.installed_str(n) = status_str("installed", :makeGreen, n)
  def self.stale_str(n) = status_str("stale", :makeYellow, n)

  FOUND_STR     = status_str("found", :makeBlue)
  SKIPPED_STR   = status_str("skipped", :makeYellow)
  BROKEN_STR    = status_str("broken", :makeRed)
  EMPTY_STR     = " " * STATUS_CELL

  # For stacks rather than packages: a stack is BUILT when the
  # compiler that names it is installed, since that is what makes it
  # usable as one. No count, and the narrow cell: nothing in that
  # listing has a number to carry.
  BUILT_STR     = status_str("built", :makeGreen, width: STATUS_LEN)
  NOT_BUILT_STR = status_str("not built", :makeRed, width: STATUS_LEN)

  public
  # host_tier controls where host packages are installed:
  #   :portable  — needs nothing from the machine (static)
  #   :distro    — links the distro's libraries
  #   :compiler  — ...and depends on the host C++ ABI
  #   :stack     — built by a compiler we built, against our sysroot
  #
  # The tier chooses the coordinates; see Package#coords.
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
    # Accept either an Array of Architecture or the {name => arch}
    # hashes several packages pass (ALL_ARCHS, X86_ARCHS), and store
    # an Array. Both worked for arch_supported?, which only asks
    # include?, but only one survives being ITERATED -- a Hash yields
    # [name, arch] pairs -- and the install scan now iterates it.
    @arch_list = arch_list.is_a?(Hash) ? arch_list.values : arch_list
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
  # the default set. Used by the host stack, which costs tens of
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

  #
  # Where this package installs, as the three toolchain5 coordinates.
  # Everything about placement derives from here; nothing else builds
  # a path by hand. See scripts/pkgmgr/coords.rb.
  #
  def coords(ver = nil)

    return Coords.new("noarch", nil, nil) if !on_host && default_arch.nil?

    if on_host
      case @host_tier
        when :portable
          # Static, or otherwise needing nothing from the machine and
          # caring about no particular compiler.
          Coords.new(HOST_OS_ARCH, nil, nil)

        when :distro
          # Links the distro's libraries, so it runs only here.
          Coords.new(HOST_OS_ARCH, HOST_DISTRO, nil)

        when :compiler
          # ...and depends on the host C++ ABI, which is a property of
          # where it can be USED, hence part of the environment.
          Coords.new(HOST_OS_ARCH, HOST_DISTRO, HOST_CC)

        when :stack
          # Built by a compiler we built, against a sysroot we
          # composed: needs nothing from the machine, but belongs to
          # exactly one stack.
          #
          # Goes through the package manager rather than building the
          # Coords here, so that a missing HOST_VER_GCC raises in one
          # place instead of quietly producing the stack "gcc-", which
          # every stack package would then share.
          pkgmgr.stack_coords(stack_gcc_ver(ver))
      end
    else
      a = default_arch

      if a.gcc_ver.nil?
        raise "#{name}: the cross-compiler version for #{a.name} is not " \
              "set yet, so its stack coordinate would be the bare " \
              "string \"gcc-\" and every arch would share one directory"
      end

      Coords.new("tilck-#{a.name}", target_board(a), "gcc-#{a.gcc_ver}")
    end
  end

  # The board a target package is built for.
  #
  # In the path because BOARD changes what gets built: before this,
  # BOARD decided WHETHER a package installed (board_supported?) but
  # never where, so two boards needing the same package would have
  # silently overwritten each other. Only safe until now because
  # board-specific packages happened to have distinct names.
  #
  # The current BOARD applies to the arch being built for; another
  # arch reached through `-a` uses its own default, since BOARD is a
  # single global and cannot mean two things at once.
  def target_board(arch)
    return BOARD if arch == ARCH && BOARD
    return arch.default_board
  end

  # Which host stack an install of `ver` belongs to.
  #
  # It is the stack this invocation is building into, which
  # `-s host_gcc:13.4.0` sets to 13.4.0 for every package in the plan,
  # so that stack's kernel headers and glibc are built alongside the
  # compiler. host_gcc overrides it, because a compiler must belong to
  # ITS OWN stack — binding one to another compiler's stack is how a
  # gcc ends up configured against a sysroot it has no business in.
  def stack_gcc_ver(ver = nil) = pkgmgr.current_host_stack

  def stack_root = coords.root

  # The composed sysroot everything in this stack is built against:
  # our glibc, our headers, and a symlink farm of the resolved
  # libraries.
  def stack_sysroot = coords.sysroot

  # Where the finished install is moved to.
  def final_install_root = coords.pkgs_dir

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

  #
  # One command in a package's build.
  #
  # `dir` is relative to the source tree, `env` is applied for the
  # step only, `unset` removes variables for it (micropython builds
  # mpy-cross for the HOST and must not inherit the cross compiler).
  #
  BuildStep = Struct.new(:log, :argv, :dir, :env, :unset,
                         keyword_init: true)

  def Step(log, argv, dir: nil, env: {}, unset: [])
    return BuildStep.new(log: log, argv: argv, dir: dir,
                         env: env, unset: unset)
  end

  #
  # The ordered commands that build this package, as DATA.
  #
  # Declarative on purpose: the same list is executed and recorded,
  # so a build cannot run a command that goes unrecorded. It takes no
  # arguments and refers to the install directory and the parallelism
  # through tokens, which keeps it computable during a staleness
  # check -- when no build is running and there is no install
  # directory to speak of.
  #
  #   $INSTALL   this version's install directory
  #   $SYSROOT   the stack's composed sysroot
  #   $PAR       the build parallelism
  #   $SRC_REF   the short git ref the source was fetched at
  #
  # $SRC_REF is why tokens exist rather than string interpolation. Its
  # value lives in a file inside the extracted source, which is not
  # there during a staleness check -- so a recipe that READ it would
  # hash differently depending on where it was asked from, which is
  # the bug that once made glycin report stale from a build directory
  # and fresh from the repository root. Naming it leaves the literal
  # "$SRC_REF" in the digest, which is the honest thing to record: the
  # value is a property of the pinned source, and the version already
  # identifies that.
  #
  # Empty means "this package builds itself imperatively"; its
  # install_impl_internal is then hashed instead. See
  # docs/plans/toolchain5.md.
  #
  def build_steps = []

  # Should the build tree be discarded once the install succeeded?
  def prune_after_build? = false

  def expand_tokens(str, install_dir)

    out = str.to_s
             .gsub("$INSTALL", install_dir.to_s)
             .gsub("$SYSROOT", (on_host ? stack_sysroot.to_s : ""))
             .gsub("$PAR", BUILD_PAR.to_s)

    # Resolved only when asked for: reading it requires the source to
    # be extracted, which is true at build time and never during a
    # staleness check.
    return out if !out.include?("$SRC_REF")
    return out.gsub("$SRC_REF", source_ref_short(install_dir))
  end

  # The short git ref the source was fetched at.
  #
  # Written by the cache beside the extracted tree when it clones a
  # git repository (.ref_name, .ref_short and .ref), so that the exact
  # commit survives independently of the .git directory. A package
  # that asks for it and has no git source is asking for something
  # that does not exist, and is told so rather than handed "".
  def source_ref_short(install_dir)

    f = Pathname.new(install_dir.to_s) / ".ref_short"

    if !f.file?
      raise "#{name}: $SRC_REF needs .ref_short, which only a source " \
            "fetched from git has"
    end

    return f.read.strip
  end

  def run_build_steps(install_dir)

    for step in build_steps do

      argv = step.argv.map { |a| expand_tokens(a, install_dir) }
      env = (step.env || {}).transform_values { |v|
        expand_tokens(v, install_dir)
      }

      ok = with_saved_env(env.keys + (step.unset || [])) do
        (step.unset || []).each { |v| ENV.delete(v) }
        env.each { |k, v| ENV[k] = v }

        dir = step.dir ? expand_tokens(step.dir, install_dir) : "."
        FileUtils.chdir(dir) { run_command(step.log, argv) }
      end

      return false if !ok
    end

    prune_build_tree if prune_after_build?
    return true
  end

  #
  # THE BUILD RECIPE: what determines the artifact, beyond its name,
  # version and coordinates.
  #
  # build_flags is the package's own arguments to whichever build
  # system it uses. The helpers ASK for it rather than accepting it,
  # so a flag that is not declared cannot be passed -- which is the
  # point: an undeclared flag would be an input nothing recorded, and
  # the install would claim to be current when it was not.
  #
  def build_flags(ver = nil) = []

  #
  # Input FILES whose content decides the artifact.
  #
  # The patch set by default, because the base class is what applies
  # patches and therefore already owns that knowledge. A package with
  # other inputs -- busybox's .config, u-boot's -- adds them.
  #
  def build_files(ver = nil)

    base = patch_base(ver)
    return [] if !base.directory?

    return Pathname.glob(base / "**" / "*.diff").sort
  end

  # Base-class helpers whose own source is part of a package's recipe
  # when it calls them. Changing --wrap-mode in meson_stack_build
  # altered dependency resolution for 22 packages at once and rebuilt
  # none of them; listing the helpers here is what catches that,
  # without making every package depend on all of package.rb.
  BUILD_HELPERS = [
    :meson_stack_build, :autotools_stack_build, :stack_install,
  ].freeze

  # Hooks that say what a package IS, or whether it may be asked for,
  # rather than how it is built. The build never calls them: they are
  # read by the package manager for placement, policy and display, so
  # changing one cannot change a produced byte.
  #
  # They are therefore excluded from the fingerprint, for the same
  # reason comments are blanked out of it. Deleting `enabled?` and a
  # duplicated `default_cc` from thirty-eight files -- a change that
  # could not alter a single installed binary -- otherwise condemned
  # 69 installs to a rebuild, including all three cross compilers.
  # A safety mechanism that charges hours for edits it can see are
  # inert is one people learn to switch off.
  #
  # Deliberately short, and deliberately not "everything the build
  # does not obviously use". default_arch stays IN: zlib's build step
  # names "#{default_arch.gcc_tc}-linux-ar", so an override of it
  # really does change the command that runs.
  NON_RECIPE_HOOKS = %i[enabled? default? default_cc].freeze

  #
  # One digest standing for "how this package is built".
  #
  # Declared flags, plus the source of the methods the package itself
  # defines, plus any shared build helper it calls. The code is in
  # here because a third of the tree does something no flag list can
  # express, and hashing it is the only way to notice a change --
  # comments excluded, so that rewriting a comment in a 109-line
  # install method does not cost a rebuild.
  #
  def build_recipe_digest(ver = nil)

    own = SourceDigest.class_source(self.class, upto: Package,
                                    except: NON_RECIPE_HOOKS)
    file = SourceDigest.source_file_of(self.class)

    helpers = BUILD_HELPERS.filter_map { |h|
      next if !own.include?(h.to_s)
      SourceDigest.method_source(__FILE__, h)
    }

    steps = build_steps.map { |st|
      [st.dir, st.unset, st.env, st.argv].inspect
    }.join("\n")

    return "sha256:" + SourceDigest.digest(
      build_flags(ver).join(" "), steps, own, *helpers
    )[0, 32]
  end

  # Evaluate `block` as the recipe reads AT one install's coordinates.
  #
  # A recipe is not one text: build_steps and build_flags may vary by
  # architecture, and several do -- zlib names its archiver
  # "#{default_arch.gcc_tc}-linux-ar", which is i686-linux-ar for one
  # install of a version and riscv64-linux-ar for another. A digest
  # therefore only means anything together with the arch it was
  # computed for, and both writing a record and checking one have to
  # say which arch they mean.
  #
  # Noarch and host packages have nothing to scope: their recipe does
  # not consult the target arch at all.
  def with_install_context(inst, &block)

    if on_host
      # The stack is a coordinate for exactly the same reason. A
      # :stack package's flags name their own sysroot -- x11 passes
      # "--libdir=.../gcc-14.4.0/sysroot/usr/lib" for one install of a
      # version and ".../gcc-16.2.0/..." for another -- so checking a
      # 16.2.0 install from an invocation whose stack is 14.4.0
      # rendered the wrong path and called twenty-two packages built
      # minutes earlier stale.
      #
      # The tier decides which host packages have a stack to scope,
      # as it does in default_cc: a :compiler-tier package also has a
      # gcc-* in its coordinates, but that one names the host's own
      # compiler and moving the stack to it would mean nothing.
      return block.call if host_tier != :stack

      stack = inst.coords&.stack_ver
      return block.call if stack.nil?
      return pkgmgr.with_host_stack(stack, &block)
    end

    return block.call if inst.arch.nil?
    return pkgmgr.with_target_arch(inst.arch, &block)
  end

  # The target architectures ONE install of this version writes.
  #
  # nil means "whatever the current scope says", which is the answer
  # for everything that produces a single tree per call. gnuefi is the
  # exception: one call builds i386 AND x86_64, so recording only the
  # current arch left the other unverifiable -- and recording every
  # install of the version instead was worse, because it stamped trees
  # this call never touched with the recipe as it reads today, quietly
  # certifying an old binary against a new recipe.
  def install_archs(ver = nil) = [nil]

  # Record what this install was built from, beside what was built.
  def write_build_inputs(inst, argv = nil)
    with_install_context(inst) {
      BuildInputs.write(inst.path,
                        recipe: build_recipe_digest(inst.ver),
                        files: build_files(inst.ver),
                        argv: argv)
    }
  end

  #
  # What an installed version's record says about it.
  #
  #   :not_installed  nothing to say
  #   :ok             built from the sources we have
  #   :changed        built from something else
  #   :unknown        no record at all
  #
  # :unknown is reported rather than assumed benign. toolchain5 starts
  # empty, so every install is made by this mechanism and a missing
  # record means something went wrong -- expat's record write raised
  # midway through the first rebuild, and gnuefi wrote one for a
  # single arch out of three. Both were invisible while a missing
  # record counted as fine. An instrument has to say when it does not
  # know.
  #
  def build_inputs_state_of(inst)

    return :not_installed if inst.nil?

    recorded = BuildInputs.comparable(inst.path)
    return :unknown if recorded.nil?

    current = with_install_context(inst) {
      BuildInputs.render(recipe: build_recipe_digest(inst.ver),
                         files: build_files(inst.ver))
    }
    return recorded == current.chomp ? :ok : :changed
  end

  # The state of the install at THIS package's coordinates.
  def build_inputs_state(ver) = build_inputs_state_of(find_install(ver))

  # Does this install need rebuilding? Both a changed recipe and a
  # missing record have the same remedy.
  def build_inputs_changed?(ver)
    return [:changed, :unknown].include?(build_inputs_state(ver))
  end

  # What this package needs from the HOST -- things pkgmgr does not
  # build and cannot install as packages of its own: a Rust toolchain,
  # a -dev library, a code generator.
  #
  # Empty for almost everything, and meant to stay that way. The
  # bootstrap in scripts/bash_includes/install_pkgs already puts a
  # compiler, make and the common -dev libraries on every machine;
  # this is for the requirements that only SOME builds have, so that
  # they are checked when that build is actually requested instead of
  # being installed on every machine forever.
  #
  # Checked across the whole transitive closure before the first
  # package is built, so a missing toolchain stops the run immediately
  # rather than halfway through it.
  #
  # Returns an array of SystemDeps::SysDep. See system_deps.rb.
  def system_deps(ver = nil) = []

  # The environment a stack package builds in.
  #
  # Everything above the toolchain has to be compiled by OUR compiler,
  # or it is not portable regardless of what the sysroot contains: the
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
  # BOTH standard directories, because replacing the search path means
  # replacing all of it. Architecture-independent packages put their
  # .pc files in share/pkgconfig — xorgproto does — and listing only
  # lib/pkgconfig loses them: libXau failed with "No package 'xproto'
  # found" while xproto.pc sat in the sysroot's share directory.
  #
  # Yields with the environment applied and restores it afterwards.
  # Where our compiler and binutils live, as [gcc_bin, binutils_bin].
  #
  # Separate from with_stack_toolchain because a package whose build
  # system is not autotools or meson has to name the compiler itself:
  # cargo takes its linker from CARGO_TARGET_<triple>_LINKER and would
  # otherwise use the system cc, producing a library that links the
  # system libc no matter what the rest of the environment says.
  def stack_toolchain_bins

    gcc = pkgmgr.stack_compiler
    gcc_inst = gcc&.find_install(gcc.default_ver)

    bu = pkgmgr.get("host_binutils")
    bu_inst = bu&.find_install(bu.default_ver)

    if gcc_inst.nil? || bu_inst.nil?
      raise "#{name}: the host toolchain is not installed; " \
            "host_gcc and host_binutils must be built first"
    end

    return [gcc_inst.path / "install" / "bin",
            bu_inst.path / "install" / "bin"]
  end

  def with_stack_toolchain(&block)

    gcc_bin, bu_bin = stack_toolchain_bins

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
      "PKG_CONFIG_LIBDIR" => "#{stack_sysroot}/usr/lib/pkgconfig:" \
                             "#{stack_sysroot}/usr/share/pkgconfig",
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

  # The shape every portable library build has.
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
  def stack_install(install_dir, &block)

    sysroot_usr = "#{stack_sysroot}/usr"
    destdir = "#{install_dir}/destdir"
    ok = false

    with_stack_toolchain { ok = block.call(sysroot_usr, destdir) }
    return false if !ok

    FileUtils.mkdir_p("#{install_dir}/install")
    FileUtils.mv("#{destdir}#{sysroot_usr}", "#{install_dir}/install/usr")

    prune_build_tree
    return true
  end

  # ./configure && make && make install, the shape most of the X11 and
  # freetype side of the QEMU closure uses.
  def autotools_stack_build(install_dir)

    return stack_install(install_dir) do |prefix, destdir|
      run_command("configure.log",
                  ["./configure", "--prefix=#{prefix}", *build_flags]) &&
      run_command("build.log", ["make", "-j#{BUILD_PAR}"]) &&
      run_command("install.log", ["make", "install", "DESTDIR=#{destdir}"])
    end
  end

  # meson + ninja, the shape glib and most of the GTK stack uses.
  #
  # --libdir=lib because the sysroot has exactly one library directory;
  # meson would otherwise pick lib64 on this host and split it.
  # meson and ninja are invoked by name: they are on PATH because they
  # publish their bin dirs and with_stack_toolchain applies what the
  # dependencies say.
  #
  # --wrap-mode=nofallback is not a detail. Meson's default is to
  # satisfy a dependency it cannot find by building the project's
  # bundled wrap of it, and that is exactly the failure this stack
  # exists to prevent: the build succeeds, nothing warns, and a second
  # copy of a library appears at whatever version the wrap happens to
  # name -- outside the version files, unlisted as a package, and
  # impossible to upgrade. pango did this with fontconfig, and was
  # caught only because both installed usr/bin/fc-cache and the
  # sysroot refused the collision; anything not installed twice would
  # have gone straight through. With nofallback the same situation is
  # a hard error naming the dependency and the version it wanted,
  # which is then a package we add deliberately.
  def meson_stack_build(install_dir)

    return stack_install(install_dir) do |prefix, destdir|
      run_command("configure.log",
                  ["meson", "setup", "build",
                   "--prefix=#{prefix}", "--libdir=lib",
                   "--buildtype=release",
                   "--wrap-mode=nofallback", *build_flags]) &&
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
  # portable C binary before composition, but not a C++ one, since
  # libstdc++ reaches the sysroot only through the graft that follows.
  #
  # Returns true when there is nothing to check.
  def post_sysroot_check(gcc_ver = nil) = true

  # Should the portability audit ask its question with a hostile
  # LD_LIBRARY_PATH?
  #
  # Yes for everything built with our toolchain: those binaries carry
  # an RPATH and must resolve correctly no matter what the environment
  # says. glibc is the exception, and the only one expected — the
  # library its own utilities need IS glibc, upstream does not rpath
  # them, and there is nothing for an RPATH to point at but the
  # loader's own home. Such a package is still audited, just without
  # the environment competing.
  def portability_hostile_check? = true

  # What this package contributes to the composed sysroot.
  #
  # A stack package contributes its whole install tree, which is
  # sysroot-shaped by convention. Everything else contributes nothing
  # unless it overrides: host_gcc is a :distro package, but its TARGET
  # runtime — libstdc++, libgcc_s — is compiled against our glibc and
  # has to be in the sysroot for anything it builds to run.
  def sysroot_fragments(gcc_ver = nil)

    return [] if host_tier != :stack

    # Ask about the stack BEING COMPOSED, not the one this invocation
    # happens to be scoped to. find_install answers at the package's
    # coordinates, and for a stack package the stack IS a coordinate,
    # so asking it from outside gives the current stack's install --
    # whose path then fails the check below and yields no fragment at
    # all. Composing every stack from a default-stack invocation
    # therefore replaced five populated sysroots with empty ones, and
    # the next compiler build stopped on
    #
    #   The directory (BUILD_SYSTEM_HEADER_DIR) that should contain
    #   system headers does not exist: .../gcc-16.2.0/sysroot/usr/include
    inst = pkgmgr.with_host_stack(gcc_ver) { find_install(default_ver) }
    return [] if inst.nil?

    # Belt and braces: the install must live in that stack. It does,
    # now that we asked the right question, but a fragment from
    # another stack is the one mistake this must never make.
    root = pkgmgr.stack_root(gcc_ver).to_s
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

  # Default arch for a regular target package: the pkgmgr's current
  # target_arch (ARCH unless a with_target_arch(...) override is
  # active). Host and noarch packages override it.
  def default_arch = pkgmgr.target_arch

  # WHICH COMPILER PRODUCES THIS PACKAGE.
  #
  # A target package is built by the cross compiler for its arch. A
  # host package in the :stack tier is built by a compiler we built
  # ourselves, and belongs to that compiler's stack -- so the answer
  # is that compiler's version, not the system's. Every other host
  # tier really is built by the system compiler.
  #
  # It used to be the literal "syscc" for every host package, spelled
  # out in thirty-odd files. That put QEMU -- built by our GCC 14.4.0,
  # living in linux-x86_64/any/gcc-14.4.0 -- in the same listing
  # section as mtools, which the distro's compiler really did build,
  # and left the stack invisible in a listing that groups on this.
  def default_cc
    return pkgmgr.target_arch.gcc_ver if !on_host
    return "syscc" if host_tier != :stack
    return pkgmgr.current_host_stack
  end
  def default_ver = pkgmgr.get_config_ver(pkg_dirname, host: on_host)

  # Every version this package can install, for the ones that offer a
  # choice. Empty means "only the default", which is almost all of
  # them: the compilers are the exception, and a caller that wants to
  # show what could be built has to ask rather than know.
  def installable_versions = []
  def pkg_dirname = name.sub("host_", "")

  # Where patches live. Overridable so that a test can point it at a
  # temporary directory instead of writing into the source tree.
  def patch_root = MAIN_DIR / "scripts" / "patches"

  #
  # Which patch directory belongs to THIS package.
  #
  # The package name, NOT pkg_dirname. pkg_dirname names a SOURCE
  # DIRECTORY, and two packages legitimately share one: host_ncurses
  # and ncurses build the same sources for different machines, and
  # gnuefi_src hands out the headers that gnuefi compiles. Their
  # INSTALL directories are still distinct, because the coordinates
  # disambiguate them -- but a patch path carries no coordinates, so
  # sharing one means a package silently receiving another's patches,
  # applied to its sources and recorded among its build inputs.
  #
  # A target package takes a "target_" prefix so that the two halves
  # read the same way round -- host_ncurses beside target_ncurses,
  # rather than beside a bare "ncurses" that looks like the default
  # someone forgot to qualify.
  #
  # Most host packages are already named host_something, but not all:
  # the musl cross-compilers are gcc-i386-musl and friends, and they
  # run on the host too. The prefix is added when it is missing rather
  # than assumed, so that the directory always says which machine the
  # package is for.
  #
  # noarch is neither, and keeps its bare name: it cannot collide,
  # since every host name starts with host_ and every target name with
  # target_.
  #
  def patch_dirname
    return name if !on_host && arch_list.nil?     # noarch
    prefix = on_host ? "host_" : "target_"
    return name.start_with?(prefix) ? name : prefix + name
  end

  def patch_base(ver = nil)
    return patch_root / patch_dirname / (ver || default_ver).to_s
  end
  def ver_dirname(ver) = ver.to_s()

  # Apply patch files from scripts/patches/<pkg>/<ver>/.
  # Applies common patches (*.diff in the version directory) first, then
  # arch-specific patches from a <arch>/ subdirectory, all in sorted order.
  # Called from install_impl after extraction, with cwd = source directory.
  #
  # Returns true on success (including "no patches to apply"), false on
  # failure. Never returns nil.
  def apply_patches(ver)

    base = patch_base(ver)
    return true if !base.directory?

    arch_name = default_arch&.name

    # Collect common patches (files directly in the version directory)
    common = Pathname.glob(base / "*.diff").sort

    # Collect arch-specific patches
    arch_specific = []
    if arch_name
      arch_dir = base / arch_name
      if arch_dir.directory?
        arch_specific = Pathname.glob(arch_dir / "*.diff").sort
      end
    end

    patches = common + arch_specific
    return true if patches.empty?

    for p in patches
      rel = p.relative_path_from(base)
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

  # The InstallInfo for `ver` at THIS package's coordinates, or nil when
  # that exact install is missing or incomplete. Single source of truth
  # for "which install do we mean": never scan get_install_list() for
  # "the first one that isn't broken", as that picks whichever version
  # the filesystem happens to list first.
  #
  # Matching on the Coords rather than on (compiler, arch) is what keeps
  # this honest. Those two are a *subset* of the coordinates -- they say
  # nothing about the board -- so an install built for one board used to
  # answer for every other: with BOARD=licheerv-nano, zlib built for
  # qemu-virt reported as installed and `-s ALL` skipped it, leaving the
  # board with no zlib at all. Anything that identifies an installation
  # by re-deriving part of its path will rot the same way the next time
  # a coordinate is added, so ask Coords, which owns that knowledge.
  def find_install(ver)
    want = coords(ver)
    return get_install_list().find { |x|
      x.ver == ver and x.coords == want and !x.broken
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
    want = coords()
    list = get_install_list.select { |x| x.coords == want && !x.broken }
    list.any? { |x| x.default_install && x.ver != default_ver }
  end

  # Does this package have anything to build at all?
  #
  # True for a prebuilt blob, and for sources another build consumes
  # in place: extracting the tarball IS the install. Distinct from an
  # empty build_steps, which means "this package builds itself
  # imperatively" -- saying so out loud beats four copies of a method
  # whose body is `true`, and keeps the recipe a declaration.
  def nothing_to_build? = false

  # Methods not implemented in the base class
  # A package declares either nothing_to_build?, build_steps, or its
  # own install_impl_internal; declaring none of the three is a
  # package that does not know how to build itself.
  def install_impl_internal(install_dir)
    return true if nothing_to_build?
    raise NotImplementedError if build_steps.empty?
    return run_build_steps(install_dir)
  end
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

  #
  # Every installation of a host package, across every coordinate it
  # could be under.
  #
  # For a :stack package that means every stack, not just the one this
  # invocation is scoped to. The stack is a coordinate, so scanning
  # only the current one makes an install in another stack invisible:
  # it cannot be found stale, it cannot be uninstalled, and its
  # sysroot fragment cannot be located -- which is how composing one
  # stack from an invocation scoped to another produced no fragments
  # and emptied five sysroots.
  #
  # The other tiers have one coordinate each and so one directory.
  #
  def syscc_package_get_install_list

    list = []
    seen = []

    for c in host_tier == :stack ? all_stack_coords : [coords] do
      next if seen.include?(c)
      seen << c

      dir = c.pkgs_dir / pkg_dirname
      next if !dir.directory?

      # The compiler is read off the coordinates being enumerated, not
      # off the package: this loop walks every stack, and an install
      # in gcc-16.2.0 was not produced by whichever stack happens to
      # be current now.
      #
      # Only for :stack, and the tier is what decides -- exactly as in
      # default_cc. A :compiler-tier package also has a gcc-* in its
      # coordinates, but that one names the HOST's compiler, which is
      # the system one: reading it here filed gtest under a stack of
      # its own.
      cc = host_tier == :stack ? (c.stack_ver || "syscc") : "syscc"

      for d in Dir.children(dir)
        ver = Ver(d.to_s)
        list << InstallInfo.new(
          name,                             # package name
          cc,                               # compiler used
          true,                             # runnning on host?
          HOST_ARCH,                        # arch
          ver,                              # package version
          dir / d,                          # install path
          self,                             # package object
          !check_install_dir(dir / d, ver), # broken?
          default_install: InstallOrigin.default_install?(dir / d),
          coords: c                         # which stack it lives in
        )
      end
    end

    return list
  end

  # The coordinates of every stack on disk, plus the current one --
  # which may not be on disk yet, during its own first install.
  def all_stack_coords

    out = pkgmgr.host_stacks.map { |v| pkgmgr.stack_coords(Ver(v)) }
    here = coords
    out << here if !out.include?(here)
    return out
  end

  # The stack directories present under one <machine>/<env>.
  def stack_dirs_of(machine, env)
    dir = TC / machine / (env || Coords::ANY)
    return [] if !dir.directory?
    return Dir.children(dir).select { |d| d.start_with?("gcc-") }
  end

  def regular_target_package_get_install_list

    list = []

    # tilck-<arch>/<board>/gcc-<ver>/pkgs/<pkg>/<ver>/ -- every
    # combination this package could have been installed under, since
    # one package may exist for several arches, boards and compilers
    # at once.
    for arch_obj in (arch_list || [])
      for board in (arch_obj.boards || [nil])
        for cc_dir in stack_dirs_of("tilck-#{arch_obj.name}", board)

          cc_ver = SafeVer(cc_dir.sub("gcc-", ""))
          next if !cc_ver

          coords = Coords.new("tilck-#{arch_obj.name}", board, cc_dir)
          dir = coords.pkgs_dir / pkg_dirname
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
              default_install: InstallOrigin.default_install?(dir / d),
              coords: coords                    # this arch+board+stack
            )
          end # for ver_dir
        end # for stack
      end # for board
    end # for arch
    return list
  end

  def noarch_package_get_install_list

    list = []
    dir = coords.pkgs_dir / pkg_dirname

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
          default_install: InstallOrigin.default_install?(dir / d),
          coords: coords                    # noarch/any/any
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


