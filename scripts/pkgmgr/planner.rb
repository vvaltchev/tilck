# SPDX-License-Identifier: BSD-2-Clause
#
# THE PLANNER: from a request to a Plan, deciding everything and
# doing nothing.
#
# Every function here takes the registry, a World, what was asked
# and a Scope, and returns a Plan or a Refusal. It reads no global,
# opens no block, holds no state, and touches no disk: the answer is
# a function of its arguments, and a test can hand it a World built
# in memory and read the Plan back. The executor runs what it
# returns; the model (tests/model/model.rb) says what it should
# return, and the two are compared on values.
#
# It knows no package. Where an install goes, which versions a package
# offers, what it depends on: those are the package's answers, asked
# of it bound to the scope in hand (Package#at). The one rule of its
# own is the -f rule: what a forced install removes is exactly what
# the install would recreate, decided from the same scope and the
# same world as the plan -- which is what makes the removal and the
# plan unable to disagree about which installation this is.
#

require 'set'
require_relative 'plan'
require_relative 'world'
require_relative 'install_selector'
require_relative 'dep_resolver'
require_relative 'version_solver'

module Planner

  module_function

  # --- dependencies, at a scope ---------------------------------------------

  # What `pkg` at `ver` depends on, under `scope`: what it declares,
  # plus the cross compiler of the scope's arch for a target package,
  # since that is what it is built with. ONE answer, read by the graph
  # and by the version binder alike: a dependency the graph knew and
  # the binder did not left the compiler with no version bound, and
  # a build with none.
  def deps_of(registry, pkg, ver, scope)
    at = pkg.at(scope)
    deps = at.check_dep_pins(at.dep_list_for(ver))
    cc = scope.arch.cross_cc_pkg
    if pkg.target? && registry.get(cc) && deps.none? { |d| d.name == cc }
      deps += [Dep(cc, true)]
    end
    return deps
  end

  # { "name" => ["dep", ...] } for every registered package. stacks:
  # false leaves the Tilck stacks' meta-packages with no dependencies,
  # for the one derivation that must not ask them (the host world).
  def graph(registry, scope, stacks: true)
    return registry.all_packages.to_h { |pkg|
      next [pkg.name, []] if !stacks && pkg.metapackage?
      [pkg.name, deps_of(registry, pkg, nil, scope).map(&:name)]
    }
  end

  # --- versions ------------------------------------------------------------

  # The version bound to every package in the closure of `requested`
  # ([[name, ver-or-nil], ...]), pins and defaults resolved together:
  # two roots pinning one dependency to different versions is a
  # conflict, found here and not by whichever was merged last. Returns
  # [bound, notes]; notes say where a pin displaced a default. Raises
  # VersionSolver's errors.
  def bind(registry, requested, scope)

    notes = []
    bound = VersionSolver.resolve(
      requested,
      deps_of: ->(n, v) {
        pkg = registry.get(n)
        pkg ? deps_of(registry, pkg, v, scope) : []
      },
      default_of: ->(n) { registry.get(n)&.at(scope)&.default_ver },
      on_override: ->(n, default_ver, pinned, path) {
        notes << "#{n}: using #{pinned}, not the default #{default_ver} " \
                 "(pinned via #{path.join(' -> ')})"
      },
    )

    return [bound, notes]
  end

  # The version of each direct dependency `pkg` at `ver` is built
  # against: what the request bound it to. What .install records as
  # `against`.
  # Every caller binds the closure first (bind), and a direct
  # dependency is in the closure, so there is no other rung. `pkg`
  # is bound: a stack's dependencies are asked at a scope.
  def against_of(pkg, ver, bound)
    return pkg.dep_list_for(ver).to_h { |d| [d.name, bound.fetch(d.name)] }
  end

  # --- what -f removes ------------------------------------------------------

  # The installations a forced install of `name` at `ver` recreates:
  # exactly those coordinates, and nothing about the compiler or the
  # arch on their own -- the coordinates ARE the compiler and the
  # arch. One per arch the install writes (gnuefi writes three).
  def forced(registry, world, name, ver, scope)

    pkg = registry.get(name)
    v = ver || pkg.at(scope).default_ver
    return pkg.at(scope).install_archs(v).filter_map { |a|
      sc = a ? scope.with(arch: a) : scope
      world.find(name, v, pkg.at(sc).coords(v))
    }
  end

  # --- the install plan ------------------------------------------------------

  # What `-s requested` does, in order: the forced removals, the
  # re-marks, then every package of the closure that is not installed
  # at its bound version, dependencies first.
  #
  #   requested   [[name, ver-or-nil], ...]
  #   force       remove what the request would recreate, first
  #   claimed     names the request speaks for: asked for by name, or
  #               the default set. Manual when built, and re-marked
  #               manual when already here as somebody's dependency
  #
  def plan_install(registry, world, requested, scope, force: false,
                   claimed: nil)

    claimed ||= requested.map(&:first)

    # Every name asked for is a package; an orphan on disk is -u's
    # business, not something to build.
    if (unknown = requested.map(&:first).find { |n| registry.get(n).nil? })
      return Refusal.new(message: "Package not found: #{unknown}")
    end

    begin
      bound, notes = bind(registry, requested, scope)
    rescue VersionSolver::ConflictError, VersionSolver::UnstableError => e
      return Refusal.new(message: "Version conflict: #{e.message}")
    end

    # Which host stack this request builds into: the host_gcc version
    # it resolves to. `-s host_gcc:13.4.0` therefore builds the 13.4.0
    # stack, headers and glibc included, rather than borrowing another
    # compiler's sysroot; a request that binds no compiler builds into
    # the stack in effect.
    scope = scope.with(stack: bound["host_gcc"] || scope.stack)

    actions = []

    if force
      for name, ver in requested do
        for inst in forced(registry, world, name, ver, scope) do
          actions << Remove.new(install: inst)
        end
      end
    end

    # The world the builds start from: what -f is about to take away
    # is not there.
    gone = actions.map(&:install).to_set
    world = World.of(world.installs.reject { |i| gone.include?(i) })

    # A package asked for by name is the user's from now on, even when
    # it was already here as somebody's dependency, at the version the
    # request means.
    for name in claimed do
      pkg = registry.get(name)
      inst = pkg.at(scope, world: world).find_install(bound[name])
      actions << Mark.new(install: inst, manual: true) if inst && !inst.manual
    end

    # What is already installed, at the version bound for it. Only the
    # closure is asked: a package outside it cannot be in the plan.
    installed = bound.select { |n, v|
      registry.get(n)&.at(scope, world: world)&.installed?(v)
    }.keys.to_set

    order = DepResolver.resolve(requested.map(&:first),
                                graph(registry, scope), installed)

    named = requested.select { |_, v| v }.map(&:first).to_set
    asked = requested.map(&:first).to_set

    # What was claimed is the user's; a root that was not claimed is
    # an upgrade and inherits the mark of what it replaces; what the
    # plan brings in besides is a dependency.
    for name in order do
      pkg = registry.get(name)
      ver = bound[name]
      moved = ver != pkg.at(scope).default_ver
      mark = if claimed.include?(name) then :manual
             elsif asked.include?(name) then inherited_mark(world, name)
             else :auto
             end
      actions << Build.new(
        name: name, ver: ver, scope: scope,
        origin: named.include?(name) || moved ? :pinned : :default,
        mark: mark, bound: bound,
        against: against_of(pkg.at(scope), ver, bound)
      )
    end

    return Plan.new(actions: actions, scope: scope, bound: bound,
                    notes: notes)
  end

  # The mark an upgrade inherits: manual if any default install of the
  # name is the user's, since that is what the new version replaces.
  def inherited_mark(world, name)
    manual = world.of(name).any? { |i|
      i.default_install && !i.broken && i.manual
    }
    return manual ? :manual : :auto
  end

  # --- upgrades, staleness, rebuilds ---------------------------------------

  # Packages with an install at the scope's coordinates that was made
  # as the default version and whose default has since moved. The
  # package says (needs_upgrade?): the stack compiler never does.
  def upgradable(registry, world, scope)
    return registry.all_packages.select { |p|
      b = p.at(scope, world: world)
      b.supported? && b.needs_upgrade?
    }
  end

  # --upgrade: the upgradable packages at their new defaults, claimed
  # by nobody -- the new version is the user's exactly as much as the
  # old was, and inherits its mark.
  def plan_upgrade(registry, world, scope)
    roots = upgradable(registry, world, scope).map { |p| [p.name, nil] }
    if roots.empty?
      return Plan.new(actions: [], scope: scope, bound: {},
                      notes: ["All installed packages are up to date"])
    end
    return plan_install(registry, world, roots, scope, claimed: [])
  end

  # Installed, but not from the sources we have now: a patch was
  # added, a flag changed, or the recipe did. Read off a JUDGED world
  # (World#judged): every install carries its record's verdict, made
  # at ITS coordinates. [package, install] pairs, dependencies first:
  # a stale glibc is rebuilt before the gcc that links it. A package
  # whose version was bumped is --upgrade's and is left out.
  def stale_installs(registry, world, scope)

    pairs = registry.all_packages.flat_map { |p|
      b = p.at(scope, world: world)
      next [] if !b.supported? || b.needs_upgrade?
      world.of(p.name).select { |i|
        raise ArgumentError, "#{p.name}: an unjudged world" if i.record.nil?
        !i.broken && i.record != :ok
      }.map { |i| [p, i] }
    }

    order = DepResolver.resolve(pairs.map { |p, _| p.name }.uniq,
                                graph(registry, scope))
    return pairs.sort_by { |p, _| order.index(p.name) }
  end

  # Why `pkg` cannot be built at `scope`, or nil: the host it needs
  # (its own, or the world it belongs to), else the arch, else the
  # board -- questions only a target package can answer "no" to.
  def unsupported_reason(registry, pkg, scope)

    closure = DepResolver.dep_closure(pkg.name, graph(registry, scope))
    for n in [pkg.name] + closure do
      p = registry.get(n)&.at(scope)
      next if p.nil? || p.host_supported?
      who = n == pkg.name ? "" : " (needs #{n}, which requires it)"
      return "host: #{pkg.name} requires #{p.host_requirement}#{who}"
    end

    b = pkg.at(scope)
    return "arch #{scope.arch.name}" if !b.arch_supported?
    return "board #{scope.board_of(scope.arch)}" if !b.board_supported?
    return nil
  end

  # --rebuild: every stale install rebuilt where it is, at its version,
  # against what it was built against, as it was asked for. Planned
  # as the install itself was, with what it was built against asked
  # for by name, so that a dependency the recipe has grown since (QEMU
  # learned libslirp) is put in first and nothing already there moves.
  # An install at a board its package does not build for is left as
  # it is, and said. An install whose dependencies cannot be known --
  # no record, two present -- refuses the whole run before anything
  # moves.
  def plan_rebuild(registry, world, scope)

    stale = stale_installs(registry, world, scope)
    if stale.empty?
      return Plan.new(actions: [], scope: scope, bound: {},
                      notes: ["Every install was built from the sources " \
                              "we have"])
    end

    notes = []
    stale, elsewhere = stale.partition { |pkg, inst|
      pkg.at(pkg.scope_at(inst, scope)).supported?
    }
    for pkg, inst in elsewhere do
      where = unsupported_reason(registry, pkg, pkg.scope_at(inst, scope))
      notes << "Left as it is: #{pkg.name}:#{inst.ver} at #{inst.coords} " \
               "(#{pkg.name} does not build for #{where})"
    end
    return Plan.new(actions: [], scope: scope, bound: {}, notes: notes) \
      if stale.empty?

    notes << "Installs to rebuild, dependencies first:"
    stale.each { |pkg, inst|
      notes << "  #{pkg.name}:#{inst.ver} at #{inst.coords}"
    }

    # What each was built against, all of it settled before anything
    # is planned around it.
    against = stale.map { |pkg, inst|
      versions, ambiguous = deps_of_install(registry, world, pkg, inst,
                                            scope)
      if !ambiguous.empty?
        return Refusal.new(message:
          "#{pkg.name}:#{inst.ver} has no record of which " \
          "#{ambiguous.join(', ')} it was built against, and more than " \
          "one is installed. Rebuild it through the package that pinned " \
          "it: -s <that package>:<ver> -f")
      end
      versions
    }

    actions = []
    seen = Set.new

    stale.zip(against).each do |(pkg, inst), versions|
      sc = pkg.scope_at(inst, scope)
      sub = plan_install(registry, world, [[pkg.name, inst.ver],
                                           *versions.to_a], sc, claimed: [])
      return sub if sub.is_a?(Refusal)

      # The dependencies grown since, once each across the plan. The
      # install itself is not among them: it is rebuilt where it is,
      # by the Replace -- at its own stack, whichever stack the request
      # resolved to. An install of a :stack package whose compiler is
      # gone resolves to the stack in effect, and a plan at that stack
      # builds the package there: a second copy at the wrong stack,
      # pinned, with the old one set aside and never put back.
      for b in sub.builds do
        next if b.name == pkg.name
        actions << b if seen.add?([b.name, b.ver, b.scope])
      end

      actions << Replace.new(install: inst, build: Build.new(
        name: pkg.name, ver: inst.ver, scope: sc,
        origin: inst.default_install ? :default : :pinned,
        mark: inst.manual ? :manual : :auto, bound: sub.bound,
        against: against_of(pkg.at(sc), inst.ver, sub.bound)
      ))
    end

    return Plan.new(actions: actions, scope: scope, bound: {}, notes: notes)
  end

  # --- the observations -------------------------------------------------------

  # --check-for-updates: [rc, lines]. Two different problems with two
  # different remedies, reported separately: a bumped version needs
  # --upgrade, a package built from sources that have since changed
  # needs a rebuild. 0 when nothing is needed, 2 otherwise.
  def check_updates(registry, world, scope)
    upgrades = upgradable(registry, world, scope).map(&:name).sort
    stale = stale_installs(registry, world, scope).map { |p, _| p.name }
                                                  .uniq.sort - upgrades
    return [0, []] if upgrades.empty? && stale.empty?
    lines = []
    lines << "NEEDS_UPGRADE #{upgrades.join(' ')}" if !upgrades.empty?
    lines << "NEEDS_REBUILD #{stale.join(' ')}" if !stale.empty?
    return [2, lines]
  end

  # --list-installable: [name, tag] in dependency order, the tag
  # "host-world" for what only the host world's roots need, "default"
  # for the default set and what it pulls in, "optional" otherwise.
  # Compilers are included: `-s <full-name>` works on them too.
  def installable(registry, scope)

    installable = registry.all_packages.reject { |p|
      p.at(scope).get_installable_list.empty?
    }
    g = graph(registry, scope)
    empty = Set.new
    defaults = registry.all_packages.select { |p| p.at(scope).default? }
    default_set = DepResolver.resolve(defaults.map(&:name), g, empty).to_set
    world = registry.host_world_names.to_set

    return DepResolver.resolve(installable.map(&:name), g, empty).map { |n|
      tag = if world.include?(n) then "host-world"
            elsif default_set.include?(n) then "default"
            else "optional"
            end
      [n, tag]
    }
  end

  # --- which installations -u means -------------------------------------------

  # What no removal may take: the interpreter this runs on.
  NEVER_REMOVE = ["ruby"].freeze

  # The coordinates an uninstall of `pkg` is about. Written out per
  # kind rather than derived, so that a reader can check each line
  # against the layout table in docs/package_manager.md.
  def uninstall_where(registry, pkg, all_pkgs, cc, arch, board, scope)

    # -c names the plain gcc stack of a version; ALL is :any, every
    # stack; "syscc" names no stack directory at all, and matches none.
    stack_of = ->(default) {
      next default if cc.nil?
      cc.is_a?(Version) ? Coords.stack_name(cc) : cc
    }

    # The board of an arch's coordinates: -b's, every one for ALL, the
    # scope's for the arch without it.
    env_of = ->(a) {
      next :any if board.eql?("ALL")
      board.nil? ? scope.board_of(a) : board
    }

    arch_of = ->(a) {
      x = a.is_a?(Architecture) ? a : ALL_ARCHS[a]
      raise ArgumentError, "Unknown arch: #{a}" if x.nil?
      x
    }

    target_at = ->(a, board, default_stack) {
      CoordsFilter.new(machine: Coords.target_machine(a), env: board,
                       stack: stack_of.call(default_stack))
    }

    # ALL: everything installed for this scope -- the target arch at
    # its current coordinates, and every host and noarch package --
    # or every arch and board with -a ALL. -c narrows to one stack.
    if all_pkgs
      st = stack_of.call(:any)
      return [CoordsFilter.new(machine: :any, env: :any, stack: st)] \
        if arch.eql?("ALL")

      a = arch.nil? ? scope.arch : arch_of.call(arch)
      return [
        CoordsFilter.new(machine: "noarch", env: :any, stack: st),
        CoordsFilter.new(machine: scope.host.machine, env: :any, stack: st),
        target_at.call(a, env_of.call(a), :any),
      ]
    end

    # An orphan has no package to say where it lives, so -a, -b and -c
    # are read directly as coordinates: an arch's machine, a board's
    # env, a stack. With none, every copy goes.
    if pkg.nil?
      st = stack_of.call(:any)
      env = (board.nil? || board.eql?("ALL")) ? :any : board
      return [CoordsFilter.new(machine: :any, env: env, stack: st)] \
        if arch.nil? || arch.eql?("ALL")
      a = arch_of.call(arch)
      return [CoordsFilter.new(machine: Coords.target_machine(a), env: env,
                               stack: st)]
    end

    at = pkg.at(scope)

    # Noarch: one place, and none of -a, -b and -c can mean anything.
    if pkg.noarch?
      return [] if !arch.nil? || !board.nil? || (!cc.nil? && cc != :any)
      return [CoordsFilter.exact(at.coords)]
    end

    # Host: -a and -b mean nothing. -c selects a stack, for a :stack
    # package.
    if pkg.on_host
      return [] if !arch.nil? || !board.nil?
      return [CoordsFilter.exact(at.coords)] if cc.nil? || cc == :any
      return [] if pkg.host_tier != :stack || !cc.is_a?(Version)
      return [CoordsFilter.exact(Coords.new(scope.host.machine, nil,
                                            Coords.stack_name(cc)))]
    end

    # Target.
    if arch.eql?("ALL")
      return ALL_ARCHS.values.map { |a| target_at.call(a, :any, :any) }
    end

    a = arch.nil? ? scope.arch : arch_of.call(arch)
    return [target_at.call(a, env_of.call(a),
                           Coords.stack_name(a.gcc_ver))]
  end

  # WHICH installations `-u` means, as one value.
  #
  # This is the argument-computing layer -- the one every -u bug lived
  # in -- so it is one function, with the coordinates written out per
  # kind of package, and the plan then asks each installation
  # `matches?` and nothing else.
  #
  #   ver       nil = the default version if it is HERE, else every
  #             version here; "ALL" = every version; a Version = that
  #             one, and if it is not here, nothing (and a note).
  #   compiler  nil = the compiler these coordinates imply; "ALL" =
  #             any; a version = the stack "gcc-<ver>".
  #   arch      nil = the scoped arch; "ALL" = every arch and board;
  #             a name = that arch at its board.
  #   board     nil = the arch's scoped board; "ALL" = every board of
  #             the arch; a name = that board.
  #   coords    an explicit list of coordinates, which beats all of the
  #             above (a forced install knows exactly what it will
  #             rewrite).
  #
  # Returns the selector, or a Refusal saying why when a named version
  # is not there.
  def selector(registry, pkg, name, install_list, scope, ver: nil,
               compiler: nil, arch: nil, board: nil, coords: nil)

    all_pkgs = name.eql?("ALL")
    # The compiler as a Version where it is one; "syscc" as the word.
    cc = if compiler.eql?("ALL") then :any
         elsif compiler.blank?   then nil
         else SafeVer(compiler.to_s) || compiler
         end
    ver = nil if ver.blank?

    where = if coords
      coords.map { |c| CoordsFilter.exact(c) }
    else
      uninstall_where(registry, pkg, all_pkgs, cc, arch, board, scope)
    end

    at_where = install_list.select { |e|
      (all_pkgs || e.pkgname == name) &&
      where.any? { |f| f.include?(e.coords) }
    }

    picked = if ver.eql?("ALL") || (ver.nil? && pkg.nil?)
      :all
    elsif ver
      if at_where.none? { |e| e.ver == ver }
        return Refusal.new(message: "#{name} #{ver} is not installed at " \
                                    "these coordinates")
      end
      ver
    else
      # No version named: the default if it is here, else everything
      # that is. Decided by what is at THESE coordinates -- the default
      # being installed somewhere else says nothing about here.
      d = pkg.at(scope).default_ver
      at_where.any? { |e| e.ver == d } ? d : :all
    end

    return InstallSelector.new(name: all_pkgs ? :all : name, ver: picked,
                               where: where)
  end

  # The installations `-u name ...` (or --mark) means, and what to say
  # about it: [installs, notes]. `except` names packages spared; ALL
  # spares the cross compilers unless `force`.
  def select(registry, world, name, scope, ver: nil, compiler: nil,
             arch: nil, board: nil, coords: nil, force: false, except: [])

    raise ArgumentError, "Invalid package name: '#{name}'" if name.blank?

    all_pkgs = name.eql?("ALL")
    pkg = all_pkgs ? nil : registry.get(name)

    # A package's installs; everything for ALL; and for a name no
    # package has, what the scan found unclaimed -- said, since it
    # is usually a typo.
    install_list, notes =
      if pkg then [world.of(name), []]
      elsif all_pkgs then [world.installs, []]
      else [world.orphans, ["Not recognized package name: #{name}"]]
      end

    sel = selector(registry, pkg, name, install_list, scope, ver: ver,
                   compiler: compiler, arch: arch, board: board,
                   coords: coords)
    return [[], notes << sel.message] if sel.is_a?(Refusal)

    picked = install_list.select { |e|
      sel.matches?(e) &&
      !except.include?(e.pkgname) &&
      !NEVER_REMOVE.include?(e.pkgname)
    }

    # Both ways of being a compiler count: the target metadata GCC's
    # installs carry, and the declaration alone. A plain -u ALL is
    # the one thing the no-force form exists to prevent.
    if all_pkgs && !force
      picked = picked.reject { |e| e.compiler? || e.pkg&.is_compiler }
    end

    return [picked, notes]
  end

  # What else exists under a name, for the note that nothing matched:
  # the usual cause is asking about one set of coordinates while it
  # lives at another.
  def elsewhere(world, name)
    return world.installs.select { |e| e.pkgname == name }.first(8).map { |e|
      "  it is installed at #{e.coords}, version #{e.ver}"
    }
  end

  def plan_uninstall(registry, world, name, scope, ver: nil, compiler: nil,
                     arch: nil, board: nil, coords: nil, force: false,
                     except: [])

    picked, notes = select(registry, world, name, scope, ver: ver,
                           compiler: compiler, arch: arch, board: board,
                           coords: coords, force: force, except: except)

    # Nothing matched, and the caller named something specific: say
    # so, and where it is. ALL is exempt -- `-u ALL` on a clean tree
    # is a no-op by design, and so is --clean.
    silent = notes.any? { |n| n&.include?("not installed") }
    if picked.empty? && !name.eql?("ALL") && !silent
      notes << "#{name}: nothing matched, so nothing was removed"
      notes += elsewhere(world, name)
    end

    return Plan.new(actions: picked.map { |i| Remove.new(install: i) },
                    scope: scope, bound: {}, notes: notes.compact)
  end

  # --mark-manual / --mark-auto: the same selection as -u, re-marked.
  def plan_mark(registry, world, name, manual, scope, ver: nil,
                compiler: nil, arch: nil, board: nil, force: false)

    picked, notes = select(registry, world, name, scope, ver: ver,
                           compiler: compiler, arch: arch, board: board,
                           force: force)
    if picked.empty? && notes.none? { |n| n&.include?("not installed") }
      notes << "#{name}: nothing matched, so nothing was marked"
    end

    marks = picked.map { |i| Mark.new(install: i, manual: manual) }
    return Plan.new(actions: marks,
                    scope: scope, bound: {}, notes: notes.compact)
  end

  # --clean: every package, every version, every compiler -- and
  # every arch, which is the one that has to be said: without it,
  # ALL means the scope's arch only.
  def plan_clean(registry, world, scope, except: [], force: false)
    return plan_uninstall(registry, world, "ALL", scope, arch: "ALL",
                          force: force, except: except)
  end

  # --- what each installation needs ------------------------------------------

  # What an install was built against, for a rebuild to build against
  # again. Recorded at install time; an install from before the record
  # is asked the only other way there is -- which version of each
  # dependency is present -- and cannot be answered when more than one
  # is. One present and none: the REQUEST's default, not the
  # install's. An install at a stack whose compiler is gone was built
  # by that compiler, but no record says so, and putting a compiler
  # back to rebuild an install nothing can use is not this question's
  # to decide: the model answers the default in effect, and so does
  # this. `scope` is the request's; the recipe is asked at the
  # install's own. Returns [versions, ambiguous], the second naming
  # the dependencies with two installs and no record.
  def deps_of_install(registry, world, pkg, inst, scope)

    recorded = InstallRecord.against(inst.path)
    ambiguous = []
    sc = pkg.scope_at(inst, scope)

    versions = pkg.at(sc).dep_list_for(inst.ver).to_h { |d|
      next [d.name, recorded[d.name]] if recorded.key?(d.name)
      next [d.name, d.ver] if d.ver
      dep = registry.get(d.name)
      here = dep ? world.of(d.name).reject(&:broken).map(&:ver).uniq : []
      ambiguous << d.name if here.length > 1
      [d.name, here.first || dep&.at(scope)&.default_ver]
    }.compact

    return [versions, ambiguous]
  end

  # Where an install of `pkg` at `ver` would write, from `scope`: the
  # -f question, asked for a dependency.
  def coords_of_install_for(pkg, ver, scope)
    return pkg.at(scope).install_archs(ver).map { |a|
      pkg.at(a ? scope.with(arch: a) : scope).coords(ver)
    }
  end

  # The installations one install needs: its dependencies at the
  # versions it was built against, each at the coordinates it would be
  # found at from this install's own scope, plus the cross compiler a
  # target install is built by. A dependency whose version cannot be
  # known -- no record, two present -- keeps every version present:
  # --autoremove must never take the one that was meant.
  def needs_of(registry, world, inst, scope)

    pkg = inst.pkg
    sc = pkg.scope_at(inst, scope)
    versions, ambiguous = deps_of_install(registry, world, pkg, inst, scope)

    wanted = versions.flat_map { |n, v|
      dep = registry.get(n)
      vers = ambiguous.include?(n) ? world.of(n).map(&:ver).uniq : [v]
      vers.map { |dv| [n, dv, coords_of_install_for(dep, dv, sc)] }
    }

    if pkg.target? && (cc = registry.get(sc.arch.cross_cc_pkg))
      v = cc.at(sc).default_ver
      wanted << [cc.name, v, coords_of_install_for(cc, v, sc)]
    end

    return wanted
  end

  # Every installation, and what each one needs among them: the graph
  # --autoremove walks and the listing counts. Built from the records
  # once per question, since a question about one root is a question
  # about every install it can reach.
  def install_graph(registry, world, scope)

    installs = world.claimed
    by_key = installs.group_by { |i| [i.pkgname, i.ver] }
    needs = {}
    missing = {}

    for i in installs do
      found = []
      gone = []

      for n, v, coords in needs_of(registry, world, i, scope) do
        here = (by_key[[n, v]] || []).select { |d| coords.include?(d.coords) }
        here.empty? ? gone << [n, v] : found.concat(here)
      end

      needs[i] = found
      missing[i] = gone
    end

    return [installs, needs, missing]
  end

  # What `roots` hold: everything they need, transitively, roots
  # included. The set --autoremove keeps is what the manual installs
  # hold; what a stack's compiler or a QEMU holds is this for one.
  def held_by(roots, needs)
    held = roots.to_set
    queue = roots.to_a
    while (i = queue.shift)
      for d in needs[i] do
        queue << d if held.add?(d)
      end
    end
    return held
  end

  # Installations that cannot be used, and what they are waiting for.
  #
  # A package can be complete, current, and still unusable: something
  # it was built against is no longer installed. It travels: an
  # install whose dependency cannot be used cannot be used either.
  # Returns {install => [what it is waiting for, as words]}.
  def unusable(installs, needs, missing)

    condemned = missing.select { |_, gone| !gone.empty? }.keys.to_set

    # Every pass condemns at least one more install or is the last,
    # and there are only so many installs: bounded by construction,
    # so no mutant of the guard below can hang the suite.
    installs.length.times do
      grew = false

      for i in installs do
        # mutation: equivalent -- a set holds what it holds
        next if condemned.include?(i)
        next if needs[i].none? { |d| condemned.include?(d) }
        condemned << i
        grew = true
      end

      break if !grew
    end

    # What each is waiting for: what is gone, else what it needs that
    # cannot be used.
    return condemned.to_h { |i|
      gone = missing[i]
      words = if !gone.empty? then gone.map { |n, v| "#{n} #{v}" }
              else needs[i].select { |d| condemned.include?(d) }
                           .map { |d| "#{d.pkgname} #{d.ver}" }.uniq
              end
      [i, words]
    }
  end

  # Every automatically installed installation that nothing kept still
  # needs -- apt's autoremove. Kept: the manual installs, and whatever
  # they need, and whatever that needs; a broken install is kept or
  # taken by the same rule, since what it is worth is not what
  # decides. Dependents go before their dependencies.
  def plan_autoremove(registry, world, scope)

    installs, needs = install_graph(registry, world, scope)
    kept = held_by(installs.select(&:manual), needs)
    removable = installs.reject { |i| kept.include?(i) }

    if removable.empty?
      return Plan.new(actions: [], scope: scope, bound: {},
                      notes: ["Nothing to remove: every automatic " \
                              "install is still needed"])
    end

    ordered = []
    pending = removable.dup
    while !pending.empty?
      free = pending.select { |i|
        pending.none? { |o| needs[o].include?(i) }
      }
      free = [pending.first] if free.empty?    # a cycle: any order will do
      ordered += free
      pending -= free
    end

    return Plan.new(actions: ordered.map { |i| Remove.new(install: i) },
                    scope: scope, bound: {}, notes: [])
  end

  # --- one request ----------------------------------------------------------

  # What `req` comes to, from `world` under `scope`: the plans, in the
  # order they run, and the world they leave. This is main.rb's
  # dispatch made a function: main parses the command line, hands the
  # Request here, prints what comes back and runs the plans; the
  # exhaustive lane hands it a world built in memory and compares the
  # world it returns with the model's. `world` is judged (World#judged)
  # for --rebuild and --check-for-updates, whose answers read the
  # records; the caller judges, since a world built in memory carries
  # its records already. The observations (-l and its kin) change
  # nothing and return the world as it was: what they print is main's.
  def step(registry, world, req, scope)
    if (why = board_refusal(req, scope))
      return Outcome.refused(world, why)
    end
    case req.mode

    when :install       then step_install(registry, world, req, scope)
    when :default       then step_default(registry, world, req, scope)
    when :upgrade       then step_upgrade(registry, world, req, scope)
    when :rebuild       then step_rebuild(registry, world, req, scope)
    when :uninstall     then step_uninstall(registry, world, req, scope)
    when :mark_manual   then step_mark(registry, world, req, scope, true)
    when :mark_auto     then step_mark(registry, world, req, scope, false)
    when :autoremove    then step_autoremove(registry, world, req, scope)
    when :clean         then step_clean(registry, world, req, scope)
    when :configure     then step_configure(registry, world, req, scope)
    when :check_updates
      rc, lines = check_updates(registry, world, scope)
      Outcome.new(rc: rc, world: world, acts: [], notes: lines, message: nil)
    when :list, :installable, :layout, :context, :other
      Outcome.ok(world)
    else
      raise ArgumentError, "unknown mode #{req.mode.inspect}"
    end
  end

  # The contributor-only extras --contrib appends to the default set.
  # Skipped silently when not registered: the list is ours.
  CONTRIB_EXTRAS = %w[host_mconf].freeze

  # A name as typed to the registered one: exact, else the one package
  # whose name contains it. [name, note-or-nil], or a Refusal.
  def resolve_name(registry, input)
    full, matches = registry.resolve_name(input)
    return [full, nil] if full == input
    return [full, "Matched '#{input}' -> '#{full}'"] if full
    if matches.empty?
      return Refusal.new(message: "Package not found: #{input}")
    end
    shown = matches.first(3).join(", ")
    suffix = matches.length > 3 ? ", ..." : ""
    return Refusal.new(message: "Ambiguous package name '#{input}' " \
                                "matches: #{shown}#{suffix}")
  end

  # A requested version, against what the package can install. Most
  # packages install one version, their default, and a request for
  # another is taken as written: nothing here knows the answer before
  # the download does. The few that offer a choice declare it in
  # installable_versions, and there a request has to name one of them
  # -- exactly, or by a series that picks one: `host_qemu:6` is
  # 6.2.0. Anything else is refused here, at the door: `host_qemu:6`
  # used to pass as the version "6", build the right compiler out of
  # its series and the whole GTK stack beneath it, and then ask for
  # qemu-6.tar.xz. The version, nil for the default, or a Refusal.
  def resolve_version(registry, name, ver)
    return nil if ver.nil?
    choices = registry.get(name).installable_versions
    return ver if choices.empty? || choices.include?(ver)
    hits = choices.select { |v| v.to_s.start_with?("#{ver}.") }
    return hits.first if hits.length == 1
    return Refusal.new(message: "#{name}:#{ver} is not a version #{name} " \
                                "can install\nAvailable: " \
                                "#{choices.join(', ')}")
  end

  # [name, ver] as typed to [registered name, version], with the note
  # a short name earns; or a Refusal.
  def resolve_target(registry, name, ver)
    full = resolve_name(registry, name)
    return full if full.is_a?(Refusal)
    full, note = full
    v = ver == :all ? :all : resolve_version(registry, full, ver)
    return v if v.is_a?(Refusal)
    return [full, v, note]
  end

  # ALL, expanded under `scope`: every installable package that is not
  # a cross compiler. Per scope, since what is installable depends on
  # the arch and the board. Compilers are reached by -S, or as the
  # dependencies they are.
  def expand_all(registry, targets, scope)
    return targets.flat_map { |name, ver|
      next [[name, ver]] if name != :all
      registry.all_packages
              .reject(&:is_compiler)
              .reject { |p| p.at(scope).get_installable_list.empty? }
              .map { |p| [p.name, ver] }
    }
  end

  # -a is a SCOPE for the modes that build, a FILTER for -u and the
  # marks: what the selector is handed for the latter, as words. The
  # arch by name: an Architecture answers `== "ALL"` with true, which
  # made every -a <arch> read as -a ALL.
  def arch_word(req) = req.every_arch? ? "ALL" : req.arch&.name
  def board_word(req) = req.every_board? ? "ALL" : req.board

  # -b, read at the door for every mode: a board belongs to one arch,
  # so a name is refused with -a ALL, and refused for an arch that
  # does not have it -- before anything is planned, filtered or read.
  # Why, or nil.
  def board_refusal(req, scope)
    return nil if !req.board.is_a?(String)
    return "-b #{req.board} names one arch's board: with -a ALL, " \
           "use -b ALL" if req.every_arch?
    a = req.arch || scope.arch
    return nil if a.all_boards.include?(req.board)
    return "Unknown board #{req.board} for #{a.name}"
  end
  def cc_word(req)
    return "ALL" if req.cc == :all
    return req.cc.is_a?(Version) ? req.cc.to_s : req.cc
  end

  # `-s`: once per arch (-a ALL threads the world through, an arch that
  # cannot build a root is skipped and said), each arch planned from
  # the world the one before it leaves. -b ALL is once per board of
  # each arch, the same way; a named board is the board the arch is
  # built for.
  def step_install(registry, world, req, scope)

    every = req.every_arch? || req.every_board?
    archs = req.every_arch? ? ALL_ARCHS.values : [req.arch || scope.arch]
    acts = []

    targets = archs.flat_map { |a|
      (req.every_board? ? a.all_boards : [req.board]).map { |b| [a, b] }

    }

    for target, board in targets do
      sc = scope.with(arch: target, board: board)
      notes = []
      label = every ? target : nil
      blabel = req.every_board? ? board : nil

      # ALL expanded inside the arch's scope, so that what is
      # installable is read for the right arch; X:ALL to every
      # version X can install, or its default when it declares none.
      requested = []
      for name, ver in expand_all(registry, req.targets, sc) do
        t = resolve_target(registry, name, ver)
        return Outcome.refused(world, t.message, acts: acts) \
          if t.is_a?(Refusal)
        full, v, note = t
        notes << note if note
        vs = v == :all ? registry.get(full).installable_versions : [v]
        (vs.empty? ? [nil] : vs).each { |x| requested << [full, x] }
      end

      # Arch AND board support for each requested package, here,
      # before -f has removed anything: the board used to be checked
      # inside the install, after the forced removal, and `-s ub -f`
      # on the wrong board deleted the install and then refused to
      # rebuild it.
      skipped = false
      for name, _ in requested do
        where = unsupported_reason(registry, registry.get(name), sc)
        next if where.nil?
        if every
          notes << "Skipping #{name}: not supported on #{where}"
          skipped = true
          break
        end
        return Outcome.refused(world, "Package #{name} is not supported " \
                                      "for #{where}", acts: acts)
      end

      if skipped
        acts << Act.make(nil, arch: label, board: blabel, notes: notes)
        next
      end

      # A package named at several versions is installed once per
      # version, in rounds, each planned from the world the one
      # before it leaves.
      rounds = rounds_of(requested)
      rounds.each_with_index { |round, i|
        if rounds.length > 1
          notes << "Round #{i + 1} of #{rounds.length}: " +
                   round.map { |n, v| v ? "#{n}:#{v}" : n }.join(" ")
        end
        plan = plan_install(registry, world, round, sc, force: req.force)
        return Outcome.refused(world, plan.message, acts: acts) \
          if plan.is_a?(Refusal)

        acts << Act.make(plan, roots: round.map(&:first), arch: label,
                         board: blabel, notes: notes)
        notes = []
        world = plan.apply(registry, world) if !req.dry
      }
    end

    return Outcome.ok(world, acts: acts)
  end

  # The rounds of a request: a package named more than once -- twice
  # by version, or through X:ALL -- goes once per version, the first
  # round taking every package's first version, the next the second
  # ones, and so on. One plan cannot hold two versions of a package:
  # each pins its own dependencies (a QEMU its compiler), and two
  # pins of one dependency are a conflict -- `-s host_qemu:6.2.0
  # host_qemu:7.2.0` planned once, into one stack, with one QEMU.
  def rounds_of(targets)
    rounds = []
    for t in targets do
      r = rounds.find { |x| x.none? { |n, _| n == t.first } }
      r ? r << t : rounds << [t]
    end
    return rounds
  end

  # No mode at all: the Tilck stack of this target -- the meta-package
  # whose dependencies are the default set -- and every installed
  # package whose version was bumped. The default set (and what
  # --contrib adds to it) is claimed -- being a default is being
  # wanted -- so one of them already here as a dependency becomes the
  # user's; the upgrades beside it are upgrades, and inherit the mark
  # of what they replace.
  def step_default(registry, world, req, scope)

    # -a <arch> and -b <board> name the target whose stack this is;
    # ALL on either is the shell's target, as the model reads it.
    scope = scope.with(
      arch: req.arch.is_a?(Architecture) ? req.arch : scope.arch,
      board: req.board.is_a?(String) ? req.board : nil
    )

    defaults = registry.tilck_stacks.select { |m| m.at(scope).supported? }
    upgrades = upgradable(registry, world, scope)
    notes = []
    if defaults.empty?
      notes << "No Tilck stack is defined for #{scope.arch.name}/" \
               "#{scope.board_of(scope.arch)}: nothing to install " \
               "by default"
    end

    all = (defaults + upgrades).uniq(&:name)
    if req.contrib
      for name in CONTRIB_EXTRAS do
        pkg = registry.get(name)
        all << pkg if pkg && all.none? { |p| p.name == name }
      end
    end

    claimed = all.map(&:name) - (upgrades.map(&:name) - defaults.map(&:name))
    plan = plan_install(registry, world, all.map { |p| [p.name, nil] },
                        scope, claimed: claimed)
    return Outcome.refused(world, plan.message, notes: notes) \
      if plan.is_a?(Refusal)

    act = Act.make(plan, roots: all.map(&:name), upgrades: upgrades.map(&:name))
    world = plan.apply(registry, world) if !req.dry
    return Outcome.ok(world, acts: [act], notes: notes)
  end

  def step_upgrade(registry, world, req, scope)
    plan = plan_upgrade(registry, world, scope)
    return Outcome.refused(world, plan.message) if plan.is_a?(Refusal)
    act = Act.make(plan, roots: plan.builds.map(&:name),
                   upgrades: plan.builds.map(&:name))
    world = plan.apply(registry, world) if !req.dry
    return Outcome.ok(world, acts: [act])
  end

  def step_rebuild(registry, world, req, scope)
    plan = plan_rebuild(registry, world, scope)
    return Outcome.refused(world, plan.message) if plan.is_a?(Refusal)
    world = plan.apply(registry, world) if !req.dry
    return Outcome.ok(world, acts: [Act.make(plan)])
  end

  # `-u`, one target at a time, each from the world the one before
  # leaves. ALL is a keyword; a name no package has but the scan found
  # -- left behind by a rename -- is removable by that name, as -l
  # lists it; anything else resolves.
  def step_uninstall(registry, world, req, scope)
    acts = []
    for name, ver in req.targets do
      if name == :all
        n, v = "ALL", ver
      elsif world.orphans.any? { |o| o.pkgname == name }
        n, v = name, ver
      else
        t = resolve_target(registry, name, ver)
        return Outcome.refused(world, t.message, acts: acts) \
          if t.is_a?(Refusal)
        n, v, note = t
        acts << Act.make(nil, notes: [note]) if note
      end
      plan = plan_uninstall(registry, world, n, scope,
                            ver: v == :all ? "ALL" : v,
                            compiler: cc_word(req), arch: arch_word(req),
                            board: board_word(req), force: req.force)
      acts << Act.make(plan)
      world = plan.apply(registry, world) if !req.dry
    end
    return Outcome.ok(world, acts: acts)
  end

  # --mark-manual / --mark-auto: the same selection as -u, re-marked.
  def step_mark(registry, world, req, scope, manual)
    acts = []
    for name, ver in req.targets do
      if name == :all
        n, v = "ALL", ver
      else
        t = resolve_target(registry, name, ver)
        return Outcome.refused(world, t.message, acts: acts) \
          if t.is_a?(Refusal)
        n, v, note = t
        acts << Act.make(nil, notes: [note]) if note
      end
      plan = plan_mark(registry, world, n, manual, scope,
                       ver: v == :all ? "ALL" : v,
                       compiler: cc_word(req), arch: arch_word(req),
                       board: board_word(req), force: req.force)
      acts << Act.make(plan)
      world = plan.apply(registry, world) if !req.dry
    end
    return Outcome.ok(world, acts: acts)
  end

  def step_autoremove(registry, world, req, scope)
    plan = plan_autoremove(registry, world, scope)
    world = plan.apply(registry, world) if !req.dry
    return Outcome.ok(world, acts: [Act.make(plan)])
  end

  # --clean keeps the cross compilers whatever -f says: `-u ALL -f
  # -a ALL -c ALL` is the line that takes them.
  def step_clean(registry, world, req, scope)
    plan = plan_clean(registry, world, scope)
    world = plan.apply(registry, world) if !req.dry
    return Outcome.ok(world, acts: [Act.make(plan)])
  end

  # -C: nothing to plan -- the package's own configuration tool runs,
  # and main runs it -- but the same refusals at the door as
  # everywhere else, and a dry run that says what it would do.
  def step_configure(registry, world, req, scope)
    name, ver = req.targets.first
    t = resolve_target(registry, name, ver)
    return Outcome.refused(world, t.message) if t.is_a?(Refusal)
    full, v, note = t
    return Outcome.refused(world, "-C #{full}:ALL: name one version") \
      if v == :all
    pkg = registry.get(full)
    if !pkg.configurable?
      return Outcome.refused(world, "Package #{pkg.name} does not support " \
                                    "reconfiguration", notes: [note].compact)
    end
    notes = [note].compact
    if req.dry
      notes << "Dry run (-d): would reconfigure #{pkg.name} " \
               "#{v || pkg.at(scope).default_ver}, rewriting its build " \
               "configuration"
    end
    return Outcome.ok(world, acts: [Act.make(nil, roots: [full, v])],
                      notes: notes)
  end
end
