# SPDX-License-Identifier: BSD-2-Clause
#
# THE MODEL: what the package manager is supposed to do, as a program.
#
# This is the contract, written to be read. It has no I/O, no globals
# of its own, no packages and no package manager: a world is a set of
# installations, a request is a parsed command line, and every
# operation is a function from (registry, world, request, invocation)
# to (exit code, world', output). Small enough to audit in one sitting,
# and wrong in the open rather than wrong in a branch nobody reads.
#
# It exists because the implementation cannot be its own oracle. Every
# logic bug this tree has had was an operation that acted on AN
# installation when it meant a specific one, and each was plausible
# enough that a test asserting "something happened" passed. The
# exhaustive lane (tests/exhaustive/) asks the model and the
# implementation the same question for every small world and every
# command, and any disagreement is a bug in one of them -- and which
# one is a decision made in the open, here.
#
# Where the implementation and this file disagree today, this file is
# the spec: it states what is RIGHT, not what the code does. Three
# such places are marked SPEC below.
#
# What it reuses from the implementation, deliberately: Coords and
# Ver, which are pure value objects with their own tests, and ALL_ARCHS
# (the harness sets every gcc_ver). Nothing that decides anything.
#
# Validated two ways before it is trusted (tests/test_model.rb): every
# historical bug is a case with the hand-written correct answer, and
# select() is total -- it never names an installation the world does
# not have.
#

require 'set'
require_relative '../../early_logic'
require_relative '../../arch'
require_relative '../../version'
require_relative '../../coords'

module Model

  # --- types --------------------------------------------------------------

  # One installation, entire. `record` is what .build_inputs says about
  # it (:ok built from the sources we have, :changed built from
  # something else, :missing no record); `origin` is what
  # .install_origin says (:default installed as the default version,
  # :pinned asked for by name).
  Key = Data.define(:name, :ver, :coords, :record, :origin) do
    def to_s = "#{name}@#{ver} #{coords} #{record}/#{origin}"
    def same_install?(o) = name == o.name && ver == o.ver && coords == o.coords
  end

  # A package as the model sees it: no recipe, only what decides
  # placement and resolution.
  #
  #   kind         :target | :noarch | :portable | :distro | :compiler
  #                | :stack | :stack_cc (host_gcc) | :cross_cc (gcc-*-musl)
  #   deps         [[name, pin_or_nil], ...]
  #   arch_list    target only: arch NAMES it builds for
  #   board_list   target only: board names, nil = any
  #   install_archs  nil, or arch NAMES one install writes (gnuefi)
  #   target_arch  cross_cc only: the arch NAME it targets
  Shape = Data.define(:name, :kind, :versions, :default_ver, :deps,
                      :arch_list, :board_list, :default, :install_archs,
                      :target_arch) do
    def self.make(name, kind, versions: ["1.0.0"], default_ver: nil,
                  deps: [], arch_list: nil, board_list: nil,
                  default: false, install_archs: nil, target_arch: nil)
      vs = versions.map { |v| Ver(v) }
      new(name: name, kind: kind, versions: vs,
          default_ver: Ver(default_ver || versions.first),
          deps: deps.map { |d, p| [d, p && Ver(p)] },
          arch_list: arch_list, board_list: board_list, default: default,
          install_archs: install_archs, target_arch: target_arch)
    end

    def target?   = kind == :target
    def noarch?   = kind == :noarch
    def host?     = !target? && !noarch?
    def compiler? = kind == :cross_cc          # what `-u ALL` keeps
    def stack?    = kind == :stack
  end

  class Registry
    def initialize(shapes)
      @by_name = shapes.to_h { |s| [s.name, s] }
    end

    def [](name)  = @by_name[name]
    def names     = @by_name.keys
    def shapes    = @by_name.values
    def key?(n)   = @by_name.key?(n)

    # The cross compiler for an arch, if one is registered: a target
    # package depends on it implicitly.
    def cross_cc_for(arch)
      shapes.find { |s| s.kind == :cross_cc && s.target_arch == arch.name }
    end

    def deps_of(name, scope)
      s = self[name]
      out = s.deps.dup
      if s.target? && (cc = cross_cc_for(scope.arch))
        out << [cc.name, nil] if out.none? { |d, _| d == cc.name }
      end
      return out
    end
  end

  # The invocation's environment: the shell's ARCH and BOARD, and the
  # stack HOST_VER_GCC names. The flags are in the Request.
  Inv = Data.define(:env_arch, :env_board, :default_stack)

  # What the invocation resolves to. board_of(a) is the one rule about
  # boards: the scoped board for the scoped arch, the shell's BOARD
  # for the shell's ARCH, an arch's own default otherwise.
  Scope = Data.define(:arch, :board, :stack, :env_arch, :env_board) do
    def board_of(a)
      return board     if a == arch
      return env_board if a == env_arch && env_board
      return a.default_board
    end

    def with(arch: self.arch, stack: self.stack)
      b = arch == self.arch ? board : board_of(arch)
      Scope.new(arch: arch, board: b, stack: stack,
                env_arch: env_arch, env_board: env_board)
    end
  end

  # A parsed command line. targets: [[name, Ver | :all | nil]].
  # arch: Architecture | :all | nil.  cc: Ver | nil.
  Request = Data.define(:mode, :targets, :force, :dry, :arch, :cc, :stack)

  Outcome = Data.define(:rc, :world, :out)

  class Conflict < StandardError; end

  NEVER_REMOVE = ["ruby"].freeze

  module_function

  # --- scope ----------------------------------------------------------------

  # `-a` is a SCOPE for the modes that build (-s, the default install,
  # --list-installable): the operation happens for that arch. It is a
  # FILTER for -u, which stays in the shell's scope and narrows what it
  # removes. Same flag, two meanings, and main.rb keeps them apart the
  # same way.
  def scope(inv, req, arch_is_scope:)
    arch = inv.env_arch
    if arch_is_scope && req.arch.is_a?(Architecture)
      arch = req.arch
    end
    board = if arch == inv.env_arch && inv.env_board
      inv.env_board
    else
      arch.default_board
    end
    return Scope.new(arch: arch, board: board,
                     stack: req.stack || inv.default_stack,
                     env_arch: inv.env_arch, env_board: inv.env_board)
  end

  # --- placement ------------------------------------------------------------

  # Where an installation of `shape` at `ver` lives, under `scope`.
  # This is the table in docs/package_manager.md.
  def coords_of(shape, scope, arch: scope.arch, stack: scope.stack)
    case shape.kind
    when :noarch
      Coords.new("noarch", nil, nil)
    when :portable, :cross_cc
      Coords.new(HOST_OS_ARCH, nil, nil)
    when :distro, :stack_cc
      Coords.new(HOST_OS_ARCH, HOST_DISTRO, nil)
    when :compiler
      Coords.new(HOST_OS_ARCH, HOST_DISTRO, HOST_CC)
    when :stack
      Coords.new(HOST_OS_ARCH, nil, Coords.stack_name(stack))
    when :target
      Coords.new("tilck-#{arch.name}", scope.board_of(arch),
                 "gcc-#{arch.gcc_ver}")
    else
      raise "unknown kind #{shape.kind}"
    end
  end

  # Every coordinates one install of `shape` writes: one, or several
  # for a package that builds for more than one arch per call.
  def install_coords(shape, scope)
    return [coords_of(shape, scope)] if shape.install_archs.nil?
    return shape.install_archs.map { |a|
      coords_of(shape, scope, arch: ALL_ARCHS[a])
    }
  end

  # --- support --------------------------------------------------------------

  def arch_supported?(shape, scope)
    return true if !shape.target?
    return shape.arch_list.include?(scope.arch.name)
  end

  def board_supported?(shape, scope)
    return true if !shape.target? || shape.board_list.nil?
    return shape.board_list.include?(scope.board)
  end

  def supported?(shape, scope)
    return arch_supported?(shape, scope) && board_supported?(shape, scope)
  end

  # --- what is installed ----------------------------------------------------

  def keys_of(world, name) = world.select { |k| k.name == name }

  def installed?(world, shape, ver, scope)
    return install_coords(shape, scope).all? { |c|
      world.any? { |k| k.name == shape.name && k.ver == ver && k.coords == c }
    }
  end

  # --- version binding ------------------------------------------------------

  # A root asked for by version is a pin. A pin anywhere in the closure
  # beats a default; two pins that disagree are an error, and nothing
  # is installed.
  def bind_versions(registry, roots, scope)

    bound = {}
    pinned = {}
    queue = []

    for name, ver in roots do
      raise Conflict, "#{name}: unknown" if !registry.key?(name)
      if ver
        raise Conflict, name if pinned[name] && bound[name] != ver
        bound[name] = ver
        pinned[name] = true
      else
        bound[name] ||= registry[name].default_ver
      end
      queue << name
    end

    seen = Set.new

    while !queue.empty?
      name = queue.shift
      next if seen.include?(name)
      seen << name

      for dep, pin in registry.deps_of(name, scope) do
        raise Conflict, "#{dep}: unknown" if !registry.key?(dep)

        if pin
          raise Conflict, dep if pinned[dep] && bound[dep] != pin
          bound[dep] = pin
          pinned[dep] = true
        else
          bound[dep] ||= registry[dep].default_ver
        end

        queue << dep
      end
    end

    return [bound, pinned]
  end

  # --- the plan -------------------------------------------------------------

  # What `-s roots` builds, in dependency order: the closure of the
  # roots, cut at anything already installed at its bound version --
  # an installed node's dependencies are not walked.
  def plan(registry, world, roots, scope)

    bound, pinned = bind_versions(registry, roots, scope)

    stack = bound["host_gcc"] || scope.stack
    scope = scope.with(stack: stack)

    installed = registry.names.select { |n|
      installed?(world, registry[n], bound[n], scope)
    }.to_set

    order = []
    visiting = Set.new

    visit = ->(n) {
      next if order.include?(n) || installed.include?(n)
      next if visiting.include?(n)
      visiting << n
      registry.deps_of(n, scope).each { |d, _| visit.call(d) }
      order << n
    }

    roots.each { |n, _| visit.call(n) }

    entries = order.map { |n|
      origin = pinned[n] ? :pinned : :default
      [n, bound[n], origin]
    }

    return [entries, scope]
  end

  # --- transitions ----------------------------------------------------------

  def install(registry, world, req, scope)

    roots = req.targets
    names = roots.map(&:first)

    if (bad = names.find { |n| !registry.key?(n) })
      return Outcome.new(1, world, "Package not found: #{bad}")
    end

    # SPEC: support is checked before anything is touched, for the
    # arch AND the board. The implementation checks the board inside
    # the install, after -f has already removed the old tree.
    for n in names do
      s = registry[n]
      next if !s.target?
      if !supported?(s, scope)
        return Outcome.new(1, world, "#{n} is not supported here")
      end
    end

    # SPEC: a version conflict is found before -f removes anything.
    begin
      entries, scope = plan(registry, force_removed(registry, world, req,
                                                    scope), roots, scope)
    rescue Conflict => e
      return Outcome.new(1, world, "Version conflict: #{e.message}")
    end

    return Outcome.new(0, world, "dry run") if req.dry

    world = force_removed(registry, world, req, scope) if req.force

    if entries.empty?
      return Outcome.new(0, world, "already installed")
    end

    for name, ver, origin in entries do
      world = with_installed(registry, world, name, ver, origin, scope)
    end

    return Outcome.new(0, world, "installed")
  end

  # -f: the exact installations the requested roots would recreate,
  # removed. Nothing else -- not the other versions, not the other
  # boards, not the other stacks.
  def force_removed(registry, world, req, scope)

    return world if !req.force

    out = world.dup

    for name, ver in req.targets do
      s = registry[name]
      v = ver || s.default_ver
      for c in install_coords(s, scope) do
        out = out.reject { |k| k.name == name && k.ver == v && k.coords == c }
             .to_set
      end
    end

    return out
  end

  def with_installed(registry, world, name, ver, origin, scope)
    s = registry[name]
    out = world.dup
    for c in install_coords(s, scope) do
      out = out.reject { |k| k.name == name && k.ver == ver && k.coords == c }
      out << Key.new(name: name, ver: ver, coords: c, record: :ok,
                     origin: origin)
    end
    return out.to_set
  end

  # What `-u` names. Total: a subset of the world, always.
  def select(registry, world, req, scope)

    name, ver = req.targets.first
    return select_all(registry, world, req, scope) if name == :all

    shape = registry[name]

    # An orphan -- on disk, no package -- has nothing to say where it
    # lives, so -a and -c are read directly as coordinates: an arch's
    # machine, a stack. With neither, every copy of it goes.
    if shape.nil?
      cc = req.cc == :all ? nil : req.cc
      return keys_of(world, name).select { |k|
        (cc.nil? || k.coords.stack == "gcc-#{cc}") &&
        (req.arch.nil? || req.arch == :all ||
         k.coords.machine == "tilck-#{req.arch.name}")
      }.to_set
    end

    at = keys_of(world, name).select { |k|
      uninstall_coords?(shape, k.coords, req, scope)
    }

    picked = if ver == :all
      at
    elsif ver
      at.select { |k| k.ver == ver }
    else
      # SPEC: the fallback -- no version named, default not here --
      # removes what is at THESE coordinates, decided by these
      # coordinates. The implementation decides it by whether the
      # default is installed anywhere.
      d = shape.default_ver
      at.any? { |k| k.ver == d } ? at.select { |k| k.ver == d } : at
    end

    return picked.to_set
  end

  # Is an installation at `c` one this request is about?
  def uninstall_coords?(shape, c, req, scope)

    cc = req.cc == :all ? nil : req.cc     # -c ALL: no constraint

    if shape.noarch?
      return req.arch.nil? && cc.nil?
    end

    if shape.host?
      return false if !req.arch.nil?        # an arch means nothing here
      return c == coords_of(shape, scope) if cc.nil?
      return false if !shape.stack?         # nor a compiler, unless :stack
      return c == coords_of(shape, scope, stack: cc)
    end

    # target
    archs = case req.arch
      when :all then ALL_ARCHS.values
      when nil  then [scope.arch]
      else [req.arch]
    end

    return archs.any? { |a|
      want = coords_of(shape, scope, arch: a)
      if req.arch == :all
        # every board of every arch, and every compiler
        c.machine == want.machine && (cc.nil? || c.stack == "gcc-#{cc}")
      elsif cc
        c.machine == want.machine && c.env == want.env &&
          c.stack == "gcc-#{cc}"
      else
        c == want
      end
    }
  end

  # `-u ALL`: SPEC. Everything installed for this scope -- the target
  # arch's packages at its current coordinates, and every host and
  # noarch package -- less the cross compilers unless -f, and less
  # what a clean must never take. -a ALL widens to every arch and
  # board; -c narrows to what one compiler built.
  def select_all(registry, world, req, scope)
    return world.select { |k|
      s = registry[k.name]
      next false if NEVER_REMOVE.include?(k.name)
      next false if s&.compiler? && !req.force
      if req.cc && req.cc != :all
        next false if k.coords.stack != "gcc-#{req.cc}"
      end

      if s.nil? || !s.target?
        true
      elsif req.arch == :all
        true
      else
        a = req.arch || scope.arch
        k.coords.machine == "tilck-#{a.name}" &&
          k.coords.env == scope.board_of(a)
      end
    }.to_set
  end

  def uninstall(registry, world, req, scope)
    name, _ = req.targets.first
    if name != :all && !registry.key?(name) && keys_of(world, name).empty?
      return Outcome.new(1, world, "Package not found: #{name}")
    end
    gone = select(registry, world, req, scope)
    return Outcome.new(0, world, "dry run") if req.dry
    return Outcome.new(0, (world - gone).to_set, "removed #{gone.size}")
  end

  def clean(registry, world, req, scope)
    all = Request.new(mode: :uninstall, targets: [[:all, :all]],
                      force: req.force, dry: req.dry, arch: :all, cc: nil,
                      stack: nil)
    return uninstall(registry, world, all, scope)
  end

  # Which installed packages want a newer default: one installed AS the
  # default, at the coordinates the default would take, whose version
  # is no longer the default. A pinned install is left alone.
  def upgradable(registry, world, scope)
    return registry.shapes.select { |s|
      next false if !supported?(s, scope)
      c = coords_of(s, scope)
      world.any? { |k|
        k.name == s.name && k.coords == c && k.origin == :default &&
          k.ver != s.default_ver
      }
    }.map(&:name)
  end

  def upgrade(registry, world, req, scope)
    roots = upgradable(registry, world, scope).map { |n| [n, nil] }
    return Outcome.new(0, world, "up to date") if roots.empty?
    plain = Request.new(mode: :install, targets: roots, force: false,
                        dry: req.dry, arch: nil, cc: nil, stack: nil)
    return install(registry, world, plain, scope)
  end

  # No mode at all: the defaults, plus whatever wants upgrading.
  def default_install(registry, world, req, scope)
    names = registry.shapes.select { |s| s.default && supported?(s, scope) }
                    .map(&:name)
    names |= upgradable(registry, world, scope)
    return Outcome.new(0, world, "nothing to do") if names.empty?
    plain = Request.new(mode: :install, targets: names.map { |n| [n, nil] },
                        force: false, dry: req.dry, arch: nil, cc: nil,
                        stack: nil)
    return install(registry, world, plain, scope)
  end

  # Nothing the model knows is configurable, so -C never changes the
  # world; it either finds the package or not.
  def configure(registry, world, req, scope)
    name, _ = req.targets.first
    return Outcome.new(1, world, "Package not found") if !registry.key?(name)
    return Outcome.new(1, world, "not configurable")
  end

  # --- observations ---------------------------------------------------------

  def state_of(key)
    return key.record == :missing ? :unknown : key.record
  end

  def observe_list(registry, world, scope)
    return world.map { |k| [k.name, k.ver, k.coords, state_of(k)] }.to_set
  end

  def observe_check_updates(registry, world, scope)
    upgrades = upgradable(registry, world, scope).sort
    stale = registry.shapes.select { |s|
      supported?(s, scope) &&
        keys_of(world, s.name).any? { |k| state_of(k) != :ok }
    }.map(&:name).sort - upgrades

    rc = (upgrades.empty? && stale.empty?) ? 0 : 2
    out = []
    out << "NEEDS_UPGRADE #{upgrades.join(' ')}" if !upgrades.empty?
    out << "NEEDS_REBUILD #{stale.join(' ')}" if !stale.empty?
    return Outcome.new(rc, world, out.join("\n"))
  end

  def observe_installable(registry, world, scope)
    return registry.shapes.select { |s| supported?(s, scope) }
                   .map(&:name).to_set
  end

  def observe_layout(registry, world, scope)
    target = ->(a) {
      next nil if a.gcc_ver.nil?
      Coords.new("tilck-#{a.name}", scope.board_of(a), "gcc-#{a.gcc_ver}")
            .pkgs_dir.to_s
    }
    v = { "PKGS_TARGET" => target.call(scope.env_arch) }
    for a in ALL_ARCHS.values do
      p = target.call(a)
      v["PKGS_TARGET_#{a.name}"] = p if p
    end
    return v
  end

  # --- one step -------------------------------------------------------------

  SCOPED_BY_ARCH = %i[install default installable].freeze

  def step(registry, world, req, inv)
    sc = scope(inv, req, arch_is_scope: SCOPED_BY_ARCH.include?(req.mode))

    case req.mode
    when :install
      if req.arch == :all
        install_every_arch(registry, world, req, sc)
      else
        install(registry, world, req, sc)
      end
    when :uninstall then uninstall(registry, world, req, sc)
    when :upgrade   then upgrade(registry, world, req, sc)
    when :clean     then clean(registry, world, req, sc)
    when :configure then configure(registry, world, req, sc)
    when :default   then default_install(registry, world, req, sc)
    when :check_updates then observe_check_updates(registry, world, sc)
    when :list, :installable, :layout then Outcome.new(0, world, "")
    else raise "unknown mode #{req.mode}"
    end
  end

  # `-s X -a ALL`: once per arch, threading the world through.
  #
  # SPEC: a root this arch cannot build is skipped, and the others are
  # still installed. The implementation skips the whole arch as soon
  # as one root is unsupported, while printing that it skipped the
  # package.
  def install_every_arch(registry, world, req, sc)
    for a in ALL_ARCHS.values do
      s2 = sc.with(arch: a)
      here = req.targets.select { |n, _|
        s = registry[n]
        s.nil? || !s.target? || supported?(s, s2)
      }
      next if here.empty?
      o = install(registry, world, req.with(targets: here), s2)
      return o if o.rc != 0
      world = o.world
    end
    return Outcome.new(0, world, "installed")
  end

  # --- argv -----------------------------------------------------------------

  # The enumerated grammar, parsed independently of OptionParser so
  # that main.rb's own reading of it is under test too.
  #
  #   -s X[:V] (repeatable)  -u X[:V]  -S ARCH  -U ARCH  -f  -d
  #   -a ARCH|ALL  -c VER  -H STACK  --upgrade  --clean  -C X[:V]
  #   -l  --check-for-updates  --list-installable  --print-layout
  #   (nothing)  -> the default install
  def parse(argv)

    mode = :default
    targets = []
    force = dry = false
    arch = cc = stack = nil
    a = argv.dup

    split = ->(s) {
      n, v = s.split(":", 2)
      [n == "ALL" ? :all : n, v.nil? ? nil : (v == "ALL" ? :all : Ver(v))]
    }

    while !a.empty?
      t = a.shift
      case t
      when "-s" then mode = :install;   targets << split.call(a.shift)
      when "-u" then mode = :uninstall; targets << split.call(a.shift)
      when "-S" then mode = :install;   targets << ["gcc-#{a.shift}-musl", nil]
      when "-U" then mode = :uninstall; targets << ["gcc-#{a.shift}-musl", nil]
      when "-C" then mode = :configure; targets << split.call(a.shift)
      when "-f" then force = true
      when "-d" then dry = true
      when "-a"
        x = a.shift
        arch = x == "ALL" ? :all : ALL_ARCHS[x]
      when "-c"
        x = a.shift
        cc = x == "ALL" ? :all : Ver(x)
      when "-H" then stack = Coords.parse_stack(a.shift)
      when "--upgrade"           then mode = :upgrade
      when "--clean"             then mode = :clean
      when "-l"                  then mode = :list
      when "--check-for-updates" then mode = :check_updates
      when "--list-installable"  then mode = :installable
      when "--print-layout"      then mode = :layout
      else raise "model: unknown argv token #{t}"
      end
    end

    return Request.new(mode: mode, targets: targets, force: force, dry: dry,
                       arch: arch, cc: cc, stack: stack)
  end

  # --- worlds ---------------------------------------------------------------

  def key(name, ver, coords, record: :ok, origin: :default)
    Key.new(name: name, ver: Ver(ver), coords: coords, record: record,
            origin: origin)
  end

  def world(*keys) = keys.to_set
end
