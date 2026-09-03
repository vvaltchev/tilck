# SPDX-License-Identifier: BSD-2-Clause

#
# Which version of each package a resolution uses.
#
# The rule, in one line: a package installed at its default version
# implicitly wants the default version of everything it depends on, and
# a package at a non-default version declares only the dependencies
# whose version has to differ.
#
# That keeps other/host_pkg_versions meaningful as a single coherent
# set — nobody has to repeat the default version at every edge — while
# still letting one package pull an older library without a matching
# entry for every other package in its closure.
#
# Two rules make it decidable:
#
#   - an explicit pin beats an implicit default. A default is "whatever
#     is current", not a request, so a pin is more specific information
#     and wins. It is reported through on_override, because a default
#     silently not being used is worth seeing;
#
#   - two explicit pins on the same package that disagree are a hard
#     error naming both paths. Nothing can satisfy both, and picking
#     one would build against a version nobody asked for.
#
# Within one resolution a package name therefore has exactly ONE
# version — the same invariant the target side has globally.
#
# Pure: no Package objects, no filesystem, no global state. `deps_of`
# and `default_of` are callables the caller supplies.
#

require 'set'

module VersionSolver

  class ConflictError < StandardError; end
  class UnstableError < StandardError; end

  # A package's dependency list may itself depend on the version it is
  # built at, so binding a version can change the closure, which can
  # bind further versions. Iterate to a fixed point rather than
  # assuming one pass is enough — and give up loudly instead of
  # spinning if it never settles.
  MAX_PASSES = 16

  module_function

  # roots:      [[name, ver_or_nil], ...] — a non-nil root version is an
  #             explicit pin, exactly like one written in a dep_list.
  # deps_of:    ->(name, ver) { [PackageDep, ...] }
  # default_of: ->(name) { Version or nil }
  # on_override: optional ->(name, default_ver, pinned_ver, path) called
  #             once per package whose default was displaced by a pin.
  #
  # Returns { "name" => Version } covering the whole closure.
  def resolve(roots, deps_of:, default_of:, on_override: nil)

    bound = {}

    MAX_PASSES.times do
      prev = bound
      bound = single_pass(roots, prev, deps_of, default_of, nil)

      next if bound != prev

      # Settled. Replay the final pass with notes enabled, so a package
      # whose default was displaced is reported once rather than once
      # per pass.
      single_pass(roots, prev, deps_of, default_of, on_override)
      return bound
    end

    raise UnstableError,
          "version selection did not settle after #{MAX_PASSES} passes"
  end

  # One breadth-first walk of the closure, binding each name it reaches.
  # `versions` is the previous pass's result, used to decide which
  # dependency list a package publishes.
  def single_pass(roots, versions, deps_of, default_of, on_override)

    bound = {}
    pinned_via = {}     # name => path of the pin that bound it
    seen = Set.new
    queue = []

    for name, ver in roots
      if ver
        bound[name] = ver
        pinned_via[name] = [name]
      else
        bound[name] ||= default_of.call(name)
      end
      queue << [name, [name]]
    end

    while !queue.empty?
      name, path = queue.shift
      next if seen.include?(name)
      seen.add(name)

      node_ver = versions[name] || bound[name] || default_of.call(name) # mutation: equivalent -- the fixpoint iteration reaches the same binding

      for dep in deps_of.call(name, node_ver)
        dep_path = path + [dep.name]

        if dep.ver
          bind_pin(bound, pinned_via, dep, dep_path, default_of, on_override)
        else
          bound[dep.name] ||= default_of.call(dep.name)
        end

        queue << [dep.name, dep_path]
      end
    end

    return bound
  end

  def bind_pin(bound, pinned_via, dep, path, default_of, on_override)

    name = dep.name
    previous = pinned_via[name]

    if previous && bound[name] != dep.ver
      raise ConflictError,
            "#{name} is pinned to #{bound[name]} via " \
            "#{previous.join(' -> ')} and to #{dep.ver} via " \
            "#{path.join(' -> ')}"
    end

    if !previous && on_override
      default = default_of.call(name)
      if default && default != dep.ver
        on_override.call(name, default, dep.ver, path)
      end
    end

    bound[name] = dep.ver
    pinned_via[name] = path
  end
end
