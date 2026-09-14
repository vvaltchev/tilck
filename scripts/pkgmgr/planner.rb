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
    cc = "gcc-#{scope.arch.name}-musl"
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
  # against: the bound one, else the dependency's own pin, else its
  # default. What InstallDeps records.
  def against_of(registry, pkg, ver, bound, scope)
    return pkg.dep_list_for(ver).to_h { |d|
      [d.name, bound[d.name] || d.ver || registry.get(d.name)&.at(scope)
                                                            &.default_ver]
    }.compact
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
        against: against_of(registry, pkg, ver, bound, scope)
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
end
