# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT AN OPERATION IS GOING TO DO, AS A VALUE.
#
# A plan is the whole answer to a request: which installations go,
# which are re-marked, which are built -- at which coordinates, at
# which version, against which versions of their dependencies -- in
# the order it happens. It is computed once, from the world as it is
# (planner.rb), and then either printed (-d) or run (executor.rb).
# Nothing about it is stashed for a later reader to find: a build
# carries every version it will need in `bound`, so the mpfr that gcc
# pinned gmp for links the gmp gcc pinned, whichever call asks.
#

require_relative 'version'
require_relative 'scope'
require_relative 'world'
require_relative 'request'

# One build: `name` at `ver`, under `scope` (the stack in it is the
# one the request resolved to); `origin` says how the version was
# chosen (:default installed as the package's default, :pinned asked
# for by name or moved by a pin) and `mark` why it is here (:manual
# asked for, :auto pulled in as a dependency); `bound` is every
# version the request resolved, which the build reads its
# dependencies' install paths through; `against` the direct
# dependencies' versions, which the install records.
Build = Data.define(:name, :ver, :scope, :origin, :mark, :bound, :against) do
  def to_s = "build #{name} #{ver} (#{origin}, #{mark})"
end

# One installation removed: what -f takes away before rebuilding.
Remove = Data.define(:install) do
  def to_s = "remove #{install.pkgname} #{install.ver} at #{install.coords}"
end

# One installation rebuilt where it is: set aside, built again by
# `build`, and put back if the build does not finish.
Replace = Data.define(:install, :build) do
  def to_s = "replace #{install.pkgname} #{install.ver} at #{install.coords}"
end

# One installation re-marked manual or auto.
Mark = Data.define(:install, :manual) do
  def to_s
    "mark #{install.pkgname} #{install.ver} #{manual ? 'manual' : 'auto'}"
  end
end

# The plan: actions in the order they run, the scope they run under
# (the stack the request resolved to, in particular), every version
# the request bound, and what the planning had to say (a pin that
# displaced a default, for instance).
Plan = Data.define(:actions, :scope, :bound, :notes) do
  def builds  = actions.grep(Build) + actions.grep(Replace).map(&:build)
  def replaces = actions.grep(Replace)
  def removes = actions.grep(Remove)
  def marks   = actions.grep(Mark)
  def empty?  = actions.empty?
  def to_s    = actions.map(&:to_s).join("\n")

  # The world this plan leaves, as a value: what the executor's tree
  # reads back as once every action has run. A removal takes its
  # install out; a mark re-marks it; a build adds the install of each
  # arch it writes, in place of any at the same coordinates; a replace
  # is its build. The executor is judged by this (test_executor.rb),
  # the planner threads the arches of `-a ALL` through it, and the
  # exhaustive lane compares it with the model's answer.
  def apply(registry, world)
    installs = world.installs.dup
    for a in actions do
      case a
      when Remove
        installs.delete(a.install)
      when Mark
        installs.map! { |i| i == a.install ? i.with_mark(a.manual) : i }
      when Build, Replace
        b = a.is_a?(Replace) ? a.build : a
        pkg = registry.get(b.name)
        for arch in pkg.at(b.scope).install_archs(b.ver) do
          sc = arch ? b.scope.with(arch: arch) : b.scope
          made = pkg.at(sc).future_install(
            b.ver, default_install: b.origin == :default,
            manual: b.mark == :manual
          )
          installs.reject! { |i|
            i.pkgname == made.pkgname && i.ver == made.ver &&
              i.coords == made.coords
          }
          installs << made
        end
      else
        raise ArgumentError, "unknown action #{a.inspect}"
      end
    end
    return World.of(installs)
  end
end

# What a request comes to (Planner.step): the exit code, the acts to
# run in the order they run -- one per arch for `-a ALL`, one per
# target for -u -- and the world every plan leaves, threaded through
# Plan#apply. An act is a plan with what to say beside it: the roots
# the request named (for the tree main draws), the arch when the run
# is per arch and the board when it is per board too, notes to print
# first, and which of the builds are upgrades. An act with no plan is
# a note alone (an arch skipped). A dry run has its acts and leaves
# the world as it was. `message` is why the exit code is not zero, or
# nil.
Act = Data.define(:plan, :roots, :arch, :board, :notes, :upgrades) do
  def self.make(plan, roots: [], arch: nil, board: nil, notes: [],
                upgrades: [])
    new(plan: plan, roots: roots, arch: arch, board: board, notes: notes,
        upgrades: upgrades)
  end

end

Outcome = Data.define(:rc, :world, :acts, :notes, :message) do
  def self.ok(world, acts: [], notes: [])
    new(rc: 0, world: world, acts: acts, notes: notes, message: nil)
  end

  def self.refused(world, message, rc: 1, acts: [], notes: [])
    new(rc: rc, world: world, acts: acts, notes: notes, message: message)
  end

  def plans = acts.filter_map(&:plan)
end

# A request the planner will not turn into a plan, and why.
Refusal = Data.define(:message) do
  def to_s = message
end
