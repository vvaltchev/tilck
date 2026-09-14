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
end

# A request the planner will not turn into a plan, and why.
Refusal = Data.define(:message) do
  def to_s = message
end
