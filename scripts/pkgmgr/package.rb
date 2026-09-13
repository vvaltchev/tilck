# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'term'
require_relative 'source_ref'
require_relative 'package_manager'
require_relative 'build_env'
require_relative 'coords'
require_relative 'scope'
require_relative 'world'
require_relative 'planner'
require_relative 'build_inputs'
require_relative 'recipe'
require_relative 'postcondition'

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
# How an installation came to be, recorded in a hidden file inside its
# version directory at install time: two facts, two words.
#
# The first is how its VERSION was chosen. On disk a default install and
# one the user named are both just <pkg>/<ver>/, and the version alone
# cannot say whether it was asked for or merely current at the time.
# --upgrade needs the difference -- a version somebody pinned must not
# be replaced behind their back.
#
# The second is WHY it is here: asked for by name (manual), or pulled
# in as a dependency of something that was (auto). -u removes what it
# is told and leaves the dependencies; --autoremove takes the auto ones
# nothing kept still needs, and --mark-manual / --mark-auto move an
# install between the two, as apt-mark does.
#
# Installations from before this file carry none of it and read as a
# manual default install: nothing pinned can predate the file, and an
# install whose reason nobody wrote down is nobody's to remove.
# One-word files from before the second fact read the same way.
#
module InstallOrigin

  FILE    = ".install_origin"
  DEFAULT = "default"
  PINNED  = "pinned"
  MANUAL  = "manual"
  AUTO    = "auto"

  module_function

  def write(dir, default_install, manual)
    File.write(dir / FILE, "#{default_install ? DEFAULT : PINNED} " \
                           "#{manual ? MANUAL : AUTO}\n")
  end

  def words(dir)
    path = dir / FILE
    return [] if !path.file?
    return path.read.split
  end

  def default_install?(dir) = words(dir).first != PINNED
  def manual?(dir) = words(dir)[1] != AUTO
end

#
# Which version of each dependency an installation was built against,
# recorded in a hidden file beside it.
#
# A package cannot work this out for itself later: mpfr's own dep list
# names host_gmp with no version, and asked alone it answers gmp's
# default -- while the gcc that pulled all of this in pinned 6.1.0.
# That resolution exists only while that request is being installed.
# A rebuild of the install, months on and on its own, has to build
# against the same gmp, and this is the only place that still knows
# which one that was. Kept apart from .build_inputs on purpose: that
# file is compared to decide whether an install is stale, and what it
# was built against is not a change to what it was built from.
#
module InstallDeps

  FILE = ".built_against"

  module_function

  def write(dir, versions)
    File.write(dir / FILE, versions.map { |n, v| "#{n} #{v}\n" }.join)
  end

  # {name => Version}; empty for an install from before the record.
  def read(dir)
    path = dir / FILE
    return {} if !path.file?
    return path.read.lines.to_h { |l| n, v = l.split; [n, Ver(v)] }
  end
end

class InstallInfo

  attr_reader :pkgname, :compiler, :on_host, :arch, :ver, :path
  attr_reader :pkg, :broken, :target_arch, :libc, :default_install
  attr_reader :coords, :manual

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
    coords: nil,            # Coords: where this installation lives
    manual: true            # asked for by name, not pulled in as a dep?
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
    @manual = manual
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

  # Two readings of one installation are equal: what identifies it,
  # and what the tree says about it. A World scanned twice is then
  # equal to itself, which is the check that the scan can be trusted.
  def ==(other)
    return other.is_a?(InstallInfo) && identity == other.identity
  end

  def eql?(other) = self == other
  def hash = identity.hash

  protected

  def identity
    return [@pkgname, @ver, @coords, @path, @broken, @default_install,
            @manual, @on_host, @arch, @compiler, @target_arch, @libc]
  end

  public

  def to_s = ("I{ " +
      "pkg: #{@pkgname.ljust(20)}, comp: #{@compiler.to_s.ljust(6)}, " +
      "arch: #{((@on_host?'host_':'')+@arch.to_s).ljust(11)}, " +
      "ver: #{@ver.to_s.ljust(25)}, target: #{@target_arch.to_s.ljust(7)}, " +
      "libc: #{@libc.to_s.ljust(5)}, " +
      "path: #{@path.sub(TC.to_s + '/', '')}" +
  " }")

end

class Package

  # Run(...), Copy(...), Within(...): the constructors a build_steps
  # is written with. See recipe.rb. And Runs(...) for a postcondition.
  include Recipe::DSL
  include Postcondition::DSL

  attr_reader :name, :source, :on_host, :is_compiler, :arch_list, :dep_list
  attr_reader :host_tier, :board_list

  # --- the scope a scoped question is answered under -----------------
  #
  # A package in the registry is a declaration. Where it installs,
  # which arch it builds for, which stack it belongs to: those are
  # questions about one INVOCATION, and asking them binds the package
  # to a Scope first. A bound package is a shallow copy with one
  # field set, so the scope an answer was computed under is visible
  # at the call site that asked, and cannot be some block open up
  # the stack.
  #
  #   pkg.at(scope).coords(ver)     # this scope, visibly
  #   pkg.coords(ver)               # TRANSITION: the invocation's, counted
  #
  # The transition (docs/plans/pkgmgr-functional-core.md, step 5.1):
  # an unbound package answers from pkgmgr.scope and notes the site
  # that asked. The sites are printed at the end of a test run and
  # driven to zero step by step; then the fallback goes and an
  # unbound question raises Unbound.
  class Unbound < StandardError; end

  # file basename => Set of line numbers that asked an unbound
  # package a scoped question, and the same for one asked what is
  # installed without a World in hand (step 5.2). Read by
  # tests/run_all.rb's summary and by test_scope.rb's ceilings.
  UNBOUND_READS = Hash.new { |h, k| h[k] = Set.new }
  UNBOUND_WORLD_READS = Hash.new { |h, k| h[k] = Set.new }
  UNBOUND_VERSION_READS = Hash.new { |h, k| h[k] = Set.new }

  # A copy bound to `s`; to `world` when the caller holds one, so
  # that what is installed is this value and not the tree as the
  # manager last read it; and to `versions`, the map a request bound
  # (Plan#bound), which is what a build reads its dependencies'
  # versions through.
  def at(s, world: nil, versions: nil)
    raise ArgumentError, "#{name}: not a Scope: #{s.inspect}" \
      if !s.is_a?(Scope)
    raise ArgumentError, "#{name}: not a World: #{world.inspect}" \
      if !world.nil? && !world.is_a?(World)
    raise ArgumentError, "#{name}: not a Hash: #{versions.inspect}" \
      if !versions.nil? && !versions.is_a?(Hash)
    # clone, not dup: a package's behaviour may sit on its singleton
    # class (the tests stub default_ver and install_impl_internal
    # that way), and dup would leave it behind.
    b = clone(freeze: false)
    b.instance_variable_set(:@scope, s)
    b.instance_variable_set(:@world, world) if world
    b.instance_variable_set(:@versions, versions) if versions
    return b
  end

  # A dependency, bound exactly as this package is: with the world
  # and versions it was handed, or without them -- a dependency that
  # then asks what is installed reads the fallback and is counted,
  # and one that only publishes its build interface never reads it.
  def bound_dep(dep_name)
    return pkgmgr.get(dep_name)&.at(scope, world: @world,
                                    versions: @versions)
  end

  def bound? = !@scope.nil?

  def scope
    return @scope if @scope
    Package.note_unbound_read(UNBOUND_READS, caller_locations(1, 12))
    return pkgmgr.scope
  end

  # What is installed, as this package sees it: the World it was
  # bound with, else (TRANSITION, counted) the manager's.
  def world
    return @world if @world
    Package.note_unbound_read(UNBOUND_WORLD_READS, caller_locations(1, 12))
    return pkgmgr.world
  end

  # The versions the request being served bound, name => Version:
  # the map this package was bound with, else (TRANSITION, counted)
  # the one the manager holds for a rebuild. The version of a
  # dependency comes from here, NOT from this package's own dep
  # list: mpfr names host_gmp without a version, so asking mpfr
  # alone answers gmp's default, while the gcc that asked for all
  # four pinned something else.
  def versions
    return @versions if @versions
    Package.note_unbound_read(UNBOUND_VERSION_READS,
                              caller_locations(1, 12))
    return pkgmgr.versions_in_effect
  end

  def resolved_ver(dep_name) = versions[dep_name]

  # The first frame of ours outside this file is the site to convert:
  # the caller that held no scope, or the recipe that read one. A
  # frame in the standard library (a FileUtils.cd block) is not a site.
  def self.note_unbound_read(table, frames)
    ours = frames.select { |x| x.path.include?("/pkgmgr/") }
    f = ours.find { |x| File.basename(x.path) != "package.rb" }
    f ||= ours.first || frames.first
    table[File.basename(f.path)] << f.lineno
  end

  # Where this package may run: OS names and host arch names, nil for
  # anywhere. Constructor arguments for most; a package that is the
  # root of a world overrides them instead, because they are then a
  # statement about the world and not part of the recipe; see
  # host_world_root?.
  def host_os_list = @host_os_list
  def host_arch_list = @host_arch_list

  # Declared `default: true`, before support is considered. default?
  # is the one that answers for an invocation; this is what the
  # package SAID, which the model needs to say the same thing.
  def marked_default? = @default

  # The three kinds, by what decides their coordinates: a target
  # package builds for an arch (and a board); a noarch one is source
  # only; a host one runs here. Asked by name so that the pair of
  # tests behind each kind is written once.
  def target? = !on_host && !arch_list.nil?
  def noarch? = !on_host && arch_list.nil?

  STATUS_LEN    = 9              # "installed"
  COUNT_GAP     = 3              # the " (" and the ")" around a count

  # The width of a status cell, given how many digits the widest count
  # in the listing needs. Zero digits means no line has a count to
  # show, and the cell is just the word -- a listing where nothing is
  # installed twice reserves no room for a number that never comes.
  def self.status_cell(digits)
    return STATUS_LEN + (digits.zero? ? 0 : digits + COUNT_GAP)
  end

  # One status cell: the word in its own colour, the count of matching
  # installs in none, padded on the right to a fixed width so that the
  # field after it starts at the same column on every line.
  #
  # LEFT-aligned, not centred. Centring moved the same word between
  # adjacent lines, because a cell with a count is full and a cell
  # without one is not:
  #
  #   host_binutils   [   installed   ]      centred
  #   host_gmp        [ installed (3) ]
  #
  #   host_binutils   [ installed     ]      left-aligned
  #   host_gmp        [ installed (3) ]
  #
  # Within the count the padding goes INSIDE the brackets -- "( 4)"
  # and "(10)" are the same width -- because slack after them reads as
  # a double space before the closing "]".
  #
  # A count of one is not shown. "installed (1)" is what "installed"
  # already meant, and a column of them would bury the counts that
  # say something.
  def self.status_str(word, color, n = nil, digits: 0)

    tail = if digits.zero? || n.nil? || n < 2
      ""
    else
      " (#{n.to_s.rjust(digits)})"
    end

    pad = [status_cell(digits) - word.length - tail.length, 0].max
    return "#{Term.send(color, word)}#{tail}#{' ' * pad}"
  end

  # The same word in two greens: bright for an install somebody asked
  # for by name, dark for one that came in as a dependency -- what
  # --autoremove may take. A line standing for several installs is
  # dark only when every one of them is.
  def self.installed_str(n, digits: 0, auto: false) =
    status_str("installed", auto ? :makeDarkGreen : :makeGreen, n,
               digits: digits)

  def self.stale_str(n, digits: 0) =
    status_str("stale", :makeYellow, n, digits: digits)

  # Complete, current -- and waiting for something that is gone.
  def self.unusable_str(n, digits: 0) =
    status_str("unusable", :makeMagenta, n, digits: digits)

  def self.found_str(digits: 0) = status_str("found", :makeBlue,
                                             digits: digits)
  def self.broken_str(digits: 0) = status_str("broken", :makeRed,
                                              digits: digits)
  def self.skipped_str(digits: 0) = status_str("skipped", :makeYellow,
                                               digits: digits)
  def self.empty_str(digits: 0) = " " * status_cell(digits)

  # For stacks rather than packages: a stack is BUILT when the
  # compiler that names it is installed, since that is what makes it
  # usable as one. One that is not built is not wrong, and its cell
  # is empty: a red "not built" beside every stack nobody asked for
  # read as a fault. The brackets stay, as they do everywhere else.
  # No count -- nothing in that listing has one.
  def self.stack_cell(built)
    return "[ #{built ? status_str("built", :makeGreen) : empty_str} ]"
  end

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
  #
  # Its own declaration first (nil lists mean "any"; non-nil lists are
  # allowlists). Then the world it belongs to: a package that exists
  # only to serve roots this host cannot build -- the 42 libraries
  # under a GTK QEMU on our glibc, the maths libraries under our GCC --
  # is not supported here either, however portable it is on its own.
  # The roots say where the world runs (host_gcc and host_qemu:
  # x86_64 Linux, for now); the other fifty follow by derivation
  # rather than by fifty flags that would each have to be remembered.
  def host_supported?
    return false if !own_host_supported?
    return true if !pkgmgr.host_world_names.include?(name)
    return pkgmgr.host_world_roots.all?(&:own_host_supported?)
  end

  # Through the hooks, not the ivars: a world root overrides them.
  def own_host_supported?
    return false if host_os_list && !host_os_list.include?(HOST_OS)
    return false if host_arch_list && !host_arch_list.include?(HOST_ARCH.name)
    return true
  end

  # What a refusal says: the host this package needs, and -- when the
  # requirement is inherited from the world it belongs to rather than
  # declared by it -- whose requirement that is.
  def host_requirement

    lists = ->(p) {
      [p.host_os_list, p.host_arch_list].compact.map { |l| l.join("/") }
    }

    return "a #{lists.call(self).join(' ')} host" if !own_host_supported?

    roots = pkgmgr.host_world_roots.reject(&:own_host_supported?)
    want = roots.flat_map { |r| lists.call(r) }.uniq.join(" ")
    return "a #{want} host (it belongs to the host world of " \
           "#{roots.map(&:name).join(', ')})"
  end

  # Is the board supported? nil = any board.
  #
  # The board that applies to the arch this package would be built
  # FOR, which is not always the global one -- the same reason
  # arch_supported? reads pkgmgr.target_arch. `-a riscv64` from an
  # i386 shell asked whether u-boot supported the board "pc", refused
  # to install it, and had already force-removed it by then.
  def board_supported?

    return true if @board_list.nil?

    a = default_arch
    return true if a.nil?

    return @board_list.include?(scope.board_of(a))
  end

  # Is the scope's target arch supported by this package?
  # Noarch (arch_list nil) and host packages are always true.
  def arch_supported?
    return true if @arch_list.nil? || @on_host
    return @arch_list.include?(scope.arch)
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

  # Is this package a root of the HOST WORLD -- the compiler we build
  # ourselves and the QEMU we build with it?
  #
  # Only the roots say so. Everything else that belongs to that world
  # is derived from the dependency graph, because there are fifty-odd
  # of them and a list of fifty is fifty chances to forget one. See
  # PackageManager#host_world_names.
  #
  # The distinction matters because that world is expensive and rarely
  # wanted: a developer or a CI run needs the packages Tilck itself is
  # built from, which is what the system tests are for. Building a GCC
  # and a GTK-enabled QEMU is a thing one does deliberately.
  def host_world_root? = false

  # Should this package be auto-installed for the current config?
  # Subclasses (e.g. GccCompiler) can override for richer logic.
  # Can this package be built for the invocation at all: on this
  # host, for this board, for this arch. The one conjunction, asked by
  # everything that lists what applies here.
  def supported? = host_supported? && board_supported? && arch_supported?

  def default? = @default && supported?

  #
  # Where this package installs, as the three toolchain5 coordinates.
  # Everything about placement derives from here; nothing else builds
  # a path by hand. See scripts/pkgmgr/coords.rb.
  #
  def coords(ver = nil)

    # mutation: equivalent -- a host package always has an arch
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
  # Which board applies to an arch is the scope's to say (Scope#board_of):
  # the answer depends on the invocation, and a package reading the
  # global pair would answer for another board while judging an
  # install that belongs to this one.
  def target_board(arch) = scope.board_of(arch)

  # The BSP directory of THIS package's install: board data (device
  # tree, bootloader config) for the arch and board it is built for.
  #
  # Deliberately not the global board_bsp() from early_logic, which
  # answers for the invocation's ARCH/BOARD pair and is right for the
  # startup validation that uses it. A recipe needs the board of the
  # installation it is building or being judged against, which under
  # with_install_context is not the same thing.
  def board_bsp

    return nil if on_host

    a = default_arch
    return nil if a.nil?

    return MAIN_DIR / "other" / "bsp" / a.name / target_board(a)
  end

  # Which host stack an install of `ver` belongs to.
  #
  # It is the stack this invocation is building into, which
  # `-s host_gcc:13.4.0` sets to 13.4.0 for every package in the plan,
  # so that stack's kernel headers and glibc are built alongside the
  # compiler. host_gcc overrides it, because a compiler must belong to
  # ITS OWN stack — binding one to another compiler's stack is how a
  # gcc ends up configured against a sysroot it has no business in.
  def stack_gcc_ver(ver = nil) = scope.stack

  def stack_root = coords.root

  # The composed sysroot everything in this stack is built against:
  # our glibc, our headers, and a symlink farm of the resolved
  # libraries.
  def stack_sysroot = coords.sysroot

  # Where the finished install is moved to.
  def final_install_root = coords.pkgs_dir

  # This package's directory at some coordinates, and one version's
  # installation inside it.
  #
  # Three scans spelled `pkgs_dir / pkg_dirname` out for themselves and
  # a fourth caller used `name` instead -- which is not the same thing,
  # since pkg_dirname drops the "host_" prefix -- and created an
  # install directory that nothing could see. The layout of an
  # installation is the package's to state, once.
  def pkg_dir_at(c) = c.pkg_dir(pkg_dirname)
  def install_dir(ver) = pkg_dir_at(coords(ver)) / ver_dirname(ver)

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
  # Where this version's directory will be once the atomic move
  # completes -- the counterpart of the staging directory a build runs
  # in, and what anything pointing AT the install has to name.
  def final_install_dir(staging_path)
    ver_dir = File.basename(staging_path.to_s)
    return final_install_root / pkg_dirname / ver_dir
  end

  def final_install_prefix(staging_path)
    return final_install_dir(staging_path) / "install"
  end

  # The version currently being installed, which is NOT default_ver
  # when the user asked for another one. Taken from the staging
  # directory's own name, the same way final_install_prefix does.
  def installing_ver(staging_path)
    return Ver(File.basename(staging_path.to_s))
  end

  # The host compiler, told the C and C++ its sources are written in.
  #
  # For a configure line: GCC 15 made C23 the default, under which
  # `void g()` declares a function of no arguments, and GCC 16 made
  # C++20 the default, under which `u8" "` is an array of char8_t.
  # Sources from before either change fail under the new reading --
  # gmp 6.1.0's own configure probe on the first, GCC 11's libcody on
  # the second -- and they are not wrong, they are older. gnu17 and
  # gnu++17 are what every GCC from 8 to 15 gave them by default: the
  # dialect they were actually built under for years, named.
  #
  # On CC and CXX rather than CFLAGS: gmp replaces its ABI-tuned flags
  # with a CFLAGS it is handed, and keeps them beside a CC.
  def host_compiler_gnu17 = [
    "CC=#{HOST_CC_CMD} -std=gnu17",
    "CXX=#{HOST_CXX_CMD} -std=gnu++17",
  ]

  # Clean build artifacts from a staging directory, keeping the
  # extracted source for a rebuild. Returns true if clean succeeded;
  # the caller deletes and re-extracts otherwise.
  #
  # Two shapes of build, told apart by what they left. A build done
  # out of tree wrote everything under build/ and install/, and once
  # those are gone the source is what the tarball held: nothing to
  # ask make to do, and no Makefile to ask with. A build done in the
  # source tree left a Makefile there, and `make distclean` is what
  # knows the rest. Twenty recipes used to override this with the
  # first two lines and then call up into the third; that distclean
  # failed every time, and every resume of gcc, glibc or qemu paid an
  # extraction for it.
  #
  # Override for a tree with its own idea of clean (a shipped
  # Makefile with only a `clean` target: dtc, lua, treecmd).
  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    return true if !(dir / "Makefile").exist?

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
  # THE RECIPE: the ordered steps that build this package, as DATA.
  #
  # Declarative on purpose: the same list is executed and recorded, so
  # a build cannot run a command that goes unrecorded. It refers to
  # the install directory and the parallelism through tokens, which
  # keeps it computable during a staleness check -- when no build is
  # running and there is no install directory to speak of.
  #
  #   $INSTALL   this version's directory WHILE it is being built
  #   $FINAL     ...and where that directory ends up
  #   $SRC       the Tilck repository
  #   $CACHE     the download cache, where a source's extra files wait
  #   $PREFIX    the install prefix under $FINAL
  #   $DESTDIR   where `make install` stages it first
  #   $SYSROOT   the sysroot at this install's coordinates
  #   $STACK_SYSROOT   the composed sysroot of the stack this install
  #              BELONGS to. The same directory for a stack package;
  #              not for the compiler that defines a stack, which is a
  #              :distro package built against the stack it defines
  #   $STACK_GCC        the bin dir of the compiler that names the stack
  #   $STACK_BINUTILS   ...and of its binutils
  #   $PAR       the build parallelism
  #   $PYTHON    the interpreter host_python installed
  #   $SRC_REF   the short git ref the source was fetched at
  #
  # ...and one per package in the DEPENDENCY CLOSURE, named after it,
  # holding the directory that package was installed in:
  #
  #   "--with-gmp=$host_gmp/install"
  #
  # The closure and not the direct list, because that is what
  # deps_build_env merges: gtk3 names glib2 through what cairo
  # publishes, and the token has to resolve wherever it turns up.
  # Uppercase is a builtin; lowercase is what the recipe brought in
  # with it -- a dependency, or a value a step bound. A recipe that
  # names a package outside its closure gets "unknown token", which
  # is the right complaint about an undeclared dependency.
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
  # Empty means there is nothing to build -- a metapackage, or a
  # source-only package; see nothing_to_build?.
  #
  # The version is never absent: asked without one, a recipe is the
  # recipe of the version this package installs by default. A nil
  # would be a third thing to mean, and a flag list that compares it
  # would fail in the one place nobody runs -- host_qemu's
  # configure_flags does exactly that.
  def build_steps(ver = default_ver) = []

  # The values behind the tokens, for one install.
  #
  # $PYTHON and $SRC_REF are Procs, so they are resolved only if a
  # step actually asks: the first needs host_python installed and the
  # second needs the source extracted, both true while building and
  # neither during a staleness check. A digest records the token,
  # never the value, so the fingerprint does not move when the
  # interpreter's path does.
  def build_tokens(install_dir)
    return {
      "INSTALL" => install_dir.to_s,

      # The repository. A recipe that copies a config file in from the
      # tree names it through this, so the digest does not record
      # where somebody happened to clone Tilck.
      "SRC"     => MAIN_DIR.to_s,
      "CACHE"   => TC_CACHE.to_s,

      # Where the package will live once installed, NOT the staging
      # path it is standing in: ld bakes its library search dirs into
      # itself from --prefix, and staging stops existing the moment
      # the install completes.
      #
      # Lazy, like the two below: only a host package has a final
      # install prefix, and a target package that never names the
      # token must not be asked to produce one.
      "FINAL"   => -> { final_install_dir(install_dir).to_s },
      "PREFIX"  => -> { final_install_prefix(install_dir).to_s },
      "DESTDIR" => "#{install_dir}/destdir",
      "SYSROOT" => (on_host ? stack_sysroot.to_s : ""),

      # Through stack_gcc_ver, the same answer the listing gives to
      # "which stack is this in": host_gcc is a :distro package whose
      # own coordinates have a sysroot that is not its stack's, and
      # the compiler is configured against the stack it DEFINES.
      "STACK_SYSROOT" => on_host ? -> {
        pkgmgr.stack_sysroot(stack_gcc_ver(installing_ver(install_dir))).to_s
      } : "",

      # The STACK's compiler, not $host_gcc: a request may pin host_gcc
      # to something else for its own build (qemu does), while every
      # package in the stack is compiled by the one that names it --
      # with_stack_toolchain says so, and a cross file must agree.
      "STACK_GCC"      => -> { stack_toolchain_bins[0].to_s },
      "STACK_BINUTILS" => -> { stack_toolchain_bins[1].to_s },
      "PAR"     => BUILD_PAR.to_s,
      "PYTHON"  => -> { pkgmgr.python_interpreter.to_s },
      "SRC_REF" => -> { source_ref_short(install_dir) },
      **dependency_tokens,
      **system_dep_tokens,
    }
  end

  # One lazy token per system dependency that declares one: where the
  # host keeps that package. A keg-only Homebrew package is on no
  # default path and only `brew --prefix` knows where it is; that runs
  # when a step names the token, never when a recipe is fingerprinted.
  # A prefix the backend cannot give is refused rather than expanded
  # to "", which would quietly turn "-I$openssl/include" into
  # "-I/include".
  def system_dep_tokens
    return system_deps(default_ver).select(&:token).to_h { |d|
      [d.token, -> {
        d.location(SystemDeps.env) or
          raise Recipe::Error, "$#{d.token}: #{d.what} was not found " \
                               "on this host"
      }]
    }
  end

  # One lazy token per package in the dependency closure. A package
  # the registry does not know has no closure and so no tokens: its
  # recipe then fails on the first "$dep" it names, with "unknown
  # token", which is the right complaint -- rather than here, with
  # MissingDepError, which is the complaint about a DEPENDENCY naming
  # a package that does not exist.
  def dependency_tokens
    return {} if pkgmgr.get(name).nil?
    return pkgmgr.dep_closure(name).to_h { |d|
      [d, -> { dep_install_dir(d).to_s }]
    }
  end

  # Where a declared dependency of this package was installed: at
  # the version the request bound (see #versions), else -- with no
  # request being served, as under `-C` -- at the version this
  # package alone resolves it to, else at its default.
  #
  # Behind a token, and therefore resolved only while building. It
  # raises when the dependency is absent, and the digest is computed
  # in exactly the situations where it may well be.
  def dep_install_dir(dep)
    pkg = bound_dep(dep)
    ver = resolved_ver(dep) || own_resolution[dep] || pkg.default_ver
    return pkg.install_prefix(ver)
  end

  # The versions this package resolves its closure to when it is the
  # whole request.
  def own_resolution = Planner.bind(pkgmgr, [[name, nil]], scope).first

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

  #
  # WHERE A PACKAGE'S RECIPE RUNS.
  #
  # A plain command goes through run_command, which streams to its log
  # and prints the line somebody could paste. A command whose OUTPUT
  # the recipe reads -- gcc -dumpspecs -- is short, and takes the base
  # class's buffered path instead.
  #
  class BuildCtx < Recipe::Ctx

    def initialize(pkg, install_dir)
      @pkg = pkg
      super(root: install_dir, tokens: pkg.build_tokens(install_dir))
    end

    def run_argv(argv, log: nil, stdin: nil, capture: nil)

      return super if capture || stdin

      # As the package, not as the context: run_command has always
      # been called with the package as self, and a package may
      # replace it.
      env = spawn_env
      ok = FileUtils.chdir(dir) {
        next @pkg.send(:run_command, log, argv) if env.empty?
        @pkg.send(:run_command, log, argv, env: env)
      }

      # run_command reports only whether it worked; 1 stands for
      # "not 0", which is all a recipe can ask about here.
      return [ok ? 0 : 1, ""]
    end

    def ambient(name, &block)
      case name
      when :stack_toolchain then @pkg.with_stack_toolchain(self, &block)
      when :cargo           then @pkg.with_cargo_env(&block)
      else super
      end
    end
  end

  def run_build_steps(install_dir, ver = nil)

    Recipe.run(build_steps(ver), BuildCtx.new(self, install_dir))
    return true

  rescue Recipe::Error => e
    error "#{name}: #{e.message}"
    return false
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

  # Programs a build must not reach for on the machine. See
  # scripts/pkgmgr/shims/python3.
  SHIMS_DIR = (RUBY_SOURCE_DIR / "shims").to_s

  #
  # One digest standing for "how this package is built": the steps,
  # which are data. What is hashed is exactly what runs, and nothing
  # else -- the Ruby around a recipe is free to change, and only a
  # step it emits can move the digest. See recipe.rb.
  #
  def build_recipe_digest(ver = nil) = Recipe.digest(build_steps(ver))

  # A meta-package, in APT's sense: it builds nothing and installs an
  # empty tree -- a directory with the records every install carries
  # -- and exists for its dependencies. The Tilck stacks are the ones
  # there are (tilck_stack.rb); the no-mode run installs them.
  def metapackage? = false

  # The title of a table of this package's installs in the listing,
  # after the host stacks, or nil for the packages that get a line in
  # their section and no more. Asked of the package because it is the
  # package that knows it is the reason a stack exists.
  def own_table = nil

  # The host stack an install of this package belongs to, or nil for
  # one with no stack to scope. The tier decides, as it does in
  # default_cc: a :stack install's is in its coordinates; a
  # :compiler-tier package also has a gcc-* there, but that one names
  # the host's own compiler and moving the stack to it would mean
  # nothing. The stack compiler overrides this: its install lives in
  # the distro's env, and the stack it belongs to is the one it
  # defines.
  def stack_of_install(inst)
    return nil if host_tier != :stack
    return inst.coords&.stack_ver
  end

  # The scope one install's own coordinates describe, from `scope`:
  # its stack for a :stack install (x11 passes
  # "--libdir=.../gcc-14.4.0/sysroot/usr/lib" for one install of a
  # version and ".../gcc-16.2.0/..." for another), its arch and board
  # for a target install (u-boot picks its .config out of the BSP
  # directory, and two boards of one arch render differently). A
  # recipe is only a recipe AT some coordinates, and judging an
  # install from another's scope called twenty-two packages built
  # minutes earlier stale. Noarch and other host installs have
  # nothing to scope.
  def scope_at(inst, scope = self.scope)
    if on_host
      stack = stack_of_install(inst)
      return stack ? scope.with(stack: stack) : scope
    end
    return scope if inst.arch.nil?
    board = inst.coords&.env
    board = nil if board == Coords::ANY
    return scope.with(arch: inst.arch, board: board)
  end

  # TRANSITION: evaluate `block` under the scope an install describes
  # from the invocation's, for callers that do not yet bind the
  # package with it. Reads the manager's scope, as the block openers
  # it replaced did.
  def with_install_context(inst, &block)
    return pkgmgr.with_scope(scope_at(inst, pkgmgr.scope), &block)
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
  #   :old_format     recorded by an older digest scheme, so the two
  #                   numbers cannot be compared at all
  #   :unknown        no record at all
  #
  # :old_format is not :changed. The install may be perfect; what
  # moved is how a recipe is fingerprinted, and the remedy is not the
  # same one. Saying "built from other sources" about an install whose
  # sources did not move would be the instrument lying about which
  # question it failed to answer.
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
      BuildInputs.comparable_lines(
        BuildInputs.render(recipe: build_recipe_digest(inst.ver),
                           files: build_files(inst.ver))
      )
    }

    return :ok if recorded == current

    # It disagrees -- but a record written by an older scheme holds a
    # number this one cannot produce, so the disagreement says
    # nothing about the sources.
    # The record is there: comparable was not nil. So format_of has a
    # number to give.
    if BuildInputs.format_of(inst.path) < BuildInputs::FORMAT
      return :old_format
    end

    return :changed
  end

  # Does this install need rebuilding? Both a changed recipe and a
  # missing record have the same remedy.
  #
  # An INSTALL, never a version.
  #
  # There used to be build_inputs_state(ver) and
  # build_inputs_changed?(ver) beside this, which looked up
  # find_install(ver) -- at the CURRENT coordinates -- and asked about
  # whatever came back. That is the shape of half this tree's bugs:
  # a caller holding one installation asks about a version, the
  # ambient coordinates answer about a different installation, and the
  # answer is plausible. The listing did exactly that and called
  # packages built minutes earlier stale.
  #
  # Deleting the version-keyed pair is what makes it unwritable rather
  # than merely wrong: there is no longer a way to ask this question
  # without saying which installation you mean. A caller that has only
  # a version says so out loud -- find_install(ver) first, and the nil
  # it may get back is the honest answer to "is there one here".
  def build_inputs_changed?(inst)
    return [:changed, :old_format,
            :unknown].include?(build_inputs_state_of(inst))
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

    gcc = pkgmgr.stack_compiler && bound_dep(pkgmgr.stack_compiler.name)
    gcc_inst = gcc&.find_install(gcc.default_ver)

    bu = bound_dep("host_binutils")
    bu_inst = bu&.find_install(bu.default_ver)

    if gcc_inst.nil? || bu_inst.nil?
      raise "#{name}: the host toolchain is not installed; " \
            "host_gcc and host_binutils must be built first"
    end

    return [gcc_inst.path / "install" / "bin",
            bu_inst.path / "install" / "bin"]
  end

  # `ctx` resolves the tokens the dependencies publish their paths
  # in; the variables set here are real ones.
  def with_stack_toolchain(ctx, &block)

    gcc_bin, bu_bin = stack_toolchain_bins

    # What the dependencies publish comes first, so a build tool a
    # dependency ships is reachable by name: meson looks for ninja on
    # PATH and will not be told about it any other way.
    deps = deps_build_env.expand(ctx).env

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
    # The shims sit between what the dependencies publish and the
    # machine's own PATH. A build that was told where its interpreter
    # is finds it in deps["PATH"] and never reaches them; one that was
    # not falls through to a python3 that refuses to run and says why,
    # rather than silently using the system's -- which is how a QEMU
    # build came to run a Homebrew 3.14 with no distlib.
    #
    # Behind the dependencies on purpose: this guards the gap, it does
    # not close the door.
    vars["PATH"] = [gcc_bin, bu_bin, deps["PATH"], SHIMS_DIR,
                    ENV["PATH"]].compact.join(":")

    return with_saved_env(vars.keys) do
      vars.each { |k, v| ENV[k] = v }
      block.call
    end
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
  #
  # A package built INTO the stack's sysroot: it is configured as
  # though it already lived there, staged into a destdir, and the
  # staged tree is what gets installed.
  #
  # The commands run inside the stack's own toolchain. That is named
  # rather than spelled out, because which environment a build runs in
  # is a real difference between two builds while the compiler paths
  # it contains are a property of the coordinates -- which are already
  # in the install path.
  #
  def stack_steps(commands)
    return [
      Within(env_from: :stack_toolchain, steps: commands),
      Mkdir(path: "$INSTALL/install"),
      Move(from: "$INSTALL/destdir$SYSROOT/usr",
           to: "$INSTALL/install/usr"),
      Prune(),
    ]
  end

  def meson_commands(flags)
    return [
      Run(log: "configure.log",
          argv: ["meson", "setup", "build",
                 "--prefix=$SYSROOT/usr", "--libdir=lib",
                 "--buildtype=release",
                 "--wrap-mode=nofallback", *flags]),
      Run(log: "build.log", argv: ["ninja", "-C", "build"]),
      Run(log: "install.log",
          argv: ["meson", "install", "-C", "build",
                 "--destdir=$INSTALL/destdir"]),
    ]
  end

  def autotools_commands(flags)
    return [
      Run(log: "configure.log",
          argv: ["./configure", "--prefix=$SYSROOT/usr", *flags]),
      Run(log: "build.log", argv: ["make", "-j$PAR"]),
      Run(log: "install.log",
          argv: ["make", "install", "DESTDIR=$INSTALL/destdir"]),
    ]
  end

  def meson_stack_steps(flags) = stack_steps(meson_commands(flags))
  def autotools_stack_steps(flags) = stack_steps(autotools_commands(flags))


  # ./configure && make && make install, the shape most of the X11 and
  # freetype side of the QEMU closure uses.
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

  # Every installation of this package, as the World says: broken
  # ones included, so that a failed earlier install can be reported
  # and removed.
  def get_install_list = world.of(name)

  # The reading of the tree a World is built from: this package's
  # directories, at every coordinates it could have been installed
  # under. Called by World.scan and by nothing else.
  def read_install_list
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

  # The arch a regular target package builds for: the scope's. Host
  # and noarch packages override it.
  def default_arch = scope.arch

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
    return scope.arch.gcc_ver if !on_host
    return "syscc" if host_tier != :stack
    return scope.stack
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
      error "#{name} requires #{host_requirement}"
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

    if !@source && !metapackage?
      raise NotImplementedError,
            "#{name}: no source declared and no custom install_impl"
    end

    # --- Download (into cache/) ---

    if @source
      ok = @source.download(ver)
      return false if !ok
    end

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
      if @source
        # Fresh extraction into staging
        chdir_package_base_dir(TC_STAGING) do
          ok = @source.extract(ver, ver_dirname(ver))
          return false if !ok
        end
      else
        # A meta-package's tree is empty: the directory is all there is
        # to extract.
        FileUtils.mkdir_p(staging)
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
        # Pathname.pwd, not a shortcut: the base class must not need
        # a mixin its subclasses happen to include for its own flow
        # to run. The first package to drop the mixins found out.
        d = Pathname.pwd

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
    pkgmgr.installs_changed!

    # Everything the build produced is in place, and now it can be
    # asked whether it works. If it does not, take it back out: a
    # failed install installs nothing, and this one has only just
    # stopped being a failed build.
    if !verify_postconditions(final_ver_dir, ver)
      error "#{name}: the install does not pass its own checks; removed"
      FileUtils.rm_rf(final_ver_dir)
      pkgmgr.installs_changed!
      return false
    end

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
  def find_install(ver) = world.find(name, ver, coords(ver))

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

    # The stack compiler never does. Each of its installs IS the stack
    # it names, and its default version is whichever stack the call is
    # scoped to -- so a gcc 12.5.0 installed as the default of the
    # 12.5.0 stack, seen from the 14.4.0 one, read as a default whose
    # default had moved on, and CMake refused to build until --upgrade
    # "fixed" it. HOST_VER_GCC moving means a new stack beside this
    # one, never this one moving.
    return false if pkgmgr.stack_compiler.equal?(self)

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

    ver = installing_ver(install_dir)
    raise NotImplementedError if build_steps(ver).empty?
    return run_build_steps(install_dir, ver)
  end
  def expected_files(ver = nil) = raise NotImplementedError

  # The behavioural half of what must be true of an install -- see
  # postcondition.rb. Checked once, after the atomic move, against the
  # install where it lives; never on a scan; never in the digest.
  def postconditions(ver = default_ver) = []

  def verify_postconditions(dir, ver)
    for pc in postconditions(ver) do
      return false if !pc.check(self, dir)
    end
    return true
  end

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

  # Where this package's install will be, as the TOKEN a dependent's
  # build names it by: "$host_ncurses". Everything a package publishes
  # in build_env is relative to this, and that is what makes the
  # interface a pure function of the package -- computable before it
  # is installed, hashed without naming a machine, and resolved by the
  # dependent's build to wherever the install actually is.
  def install_token = Pathname.new("$#{name}")

  # A file in the repository, as a recipe names it: "$SRC/other/...".
  # The digest records where in the tree, never where the tree is.
  # A path outside the repository is refused, which is what it should
  # be: a recipe reads the tree and the coordinates, and nothing else.
  def src_path(path)
    rel = Pathname.new(path.to_s).relative_path_from(MAIN_DIR)
    return "$SRC/#{rel}"
  end

  # The merged build interface published by this package's dependencies,
  # each at the version bound for it, nearest dependency first.
  #
  # Consumers call this instead of naming any dependency: adding a new
  # host library to dep_list is enough for its flags to appear here.
  #
  # PURE, and written in tokens: "-I$host_ncurses/install/include".
  # Nothing here needs a dependency installed, so a recipe built from
  # it can be fingerprinted during a staleness check, and the
  # fingerprint does not record where this machine keeps its
  # toolchain. A build that must set real variables, or a shell about
  # to run menuconfig, asks for .expand(ctx) first.
  def deps_build_env

    versions = own_resolution

    return pkgmgr.dep_closure(name).reduce(BuildEnv.empty) { |acc, dep_name|
      dep = bound_dep(dep_name)
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

    # all_stack_coords is already a set: the current stack joins the
    # ones on disk without repeating.
    for c in host_tier == :stack ? all_stack_coords : [coords] do
      dir = pkg_dir_at(c)
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
      # mutation: equivalent -- a stack's coordinates always parse
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
          manual: InstallOrigin.manual?(dir / d),
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
    return out | [coords]
  end

  # The stack directories present under one <machine>/<env>.
  def stack_dirs_of(machine, env)
    dir = Coords.env_dir(machine, env)
    return [] if !dir.directory?
    return Dir.children(dir).select { |d| d.start_with?("gcc-") }
  end

  def regular_target_package_get_install_list

    list = []

    # tilck-<arch>/<board>/gcc-<ver>/pkgs/<pkg>/<ver>/ -- every
    # combination this package could have been installed under, since
    # one package may exist for several arches, boards and compilers
    # at once.
    for arch_obj in arch_list
      for board in (arch_obj.boards || [nil])
        for cc_dir in stack_dirs_of("tilck-#{arch_obj.name}", board)

          cc_ver = SafeVer(cc_dir.sub("gcc-", ""))
          next if !cc_ver

          coords = Coords.new("tilck-#{arch_obj.name}", board, cc_dir)
          dir = pkg_dir_at(coords)
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
              manual: InstallOrigin.manual?(dir / d),
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
    dir = pkg_dir_at(coords)

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
          manual: InstallOrigin.manual?(dir / d),
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


