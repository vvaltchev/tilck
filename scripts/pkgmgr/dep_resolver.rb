# SPDX-License-Identifier: BSD-2-Clause

#
# Pure dependency-resolution algorithms for the package manager.
#
# Every function operates on a plain graph hash:
#
#   { "pkg_name" => ["dep_name1", "dep_name2", ...], ... }
#
# No Package objects, no filesystem, no global state — so the module
# is trivially testable with minitest.
#

require 'set'

module DepResolver

  class CycleError < StandardError; end
  class MissingDepError < StandardError; end

  # A walk that took more steps than a finite graph allows. Not a
  # cycle -- cycles are named as such -- but a walk that is broken:
  # a loop condition inverted, a queue fed nils, a graph that grows
  # under the walk. Raised rather than hung, because a hang is the
  # one failure that nothing downstream can report.
  class NonTerminatingWalk < StandardError; end

  module_function

  # Validate that every dependency name referenced in the graph exists
  # as a key. Raises MissingDepError listing all dangling references.
  def validate_deps(graph)
    missing = []
    graph.each do |pkg, deps|
      deps.each do |d|
        if !graph.key?(d)
          missing << "#{pkg} -> #{d}"
        end
      end
    end
    if !missing.empty?
      raise MissingDepError,
            "Unknown dependencies: #{missing.join(', ')}"
    end
  end

  # Detect cycles using DFS with 3-color marking (white/gray/black).
  # Raises CycleError with the cycle path on first cycle found.
  def validate_no_cycles(graph)
    white = 0; gray = 1; black = 2
    color = {}
    graph.each_key { |k| color[k] = white }
    path = []

    visit = ->(node) {
      color[node] = gray
      path.push(node)

      for dep in graph[node]     # visited only when a key: see the loop below
        if color[dep] == gray
          cycle_start = path.index(dep)
          cycle = path[cycle_start..] + [dep]
          raise CycleError,
                "Dependency cycle: #{cycle.join(' -> ')}"
        end
        # mutation: equivalent -- revisiting black nodes finds the same cycles
        visit.call(dep) if color[dep] == white
      end

      path.pop
      color[node] = black
    }

    graph.each_key { |node| visit.call(node) if color[node] == white }
  end

  # Run both validations: missing deps first, then cycles.
  def validate(graph)
    validate_deps(graph)
    validate_no_cycles(graph)
  end

  # Transitive dependency closure of `name`, nearest first: direct
  # dependencies before their own dependencies, breadth-first.
  #
  # This is deliberately NOT the topological order computed by resolve():
  # install order needs dependencies built first, while a consumer
  # collecting build flags wants its direct dependencies' include paths
  # ahead of the transitive ones. Reversing a topological sort does not
  # give that when the graph branches.
  #
  # `name` itself is not included. Raises MissingDepError if `name` is
  # not in the graph.
  # Every walk below carries the path it came by, so that meeting a
  # node already ON that path is reported as the cycle it is, with
  # the path in the message -- not swallowed by the "seen" set that
  # keeps a diamond from being expanded twice. A cycle here means the
  # startup validation was bypassed or the graph changed under it,
  # and either is worth a name rather than a silent answer.
  #
  # And every walk is bounded. A finite graph is dequeued at most
  # once per edge plus once per root; more than that is a walk that
  # is not going to end, and it stops with NonTerminatingWalk instead
  # of hanging the process.
  def walk_limit(graph, roots = 1)
    return graph.size + graph.values.sum(&:length) + roots + 1
  end

  def check_cycle(node, path)
    return if !path[0...-1].include?(node)
    raise CycleError, "Dependency cycle: #{path.join(' -> ')}"
  end

  def dep_closure(name, graph)

    if !graph.key?(name)
      raise MissingDepError, "Unknown package: #{name}"
    end

    seen = Set.new([name])
    out = []
    queue = graph[name].map { |d| [d, [name, d]] }
    limit = walk_limit(graph)
    steps = 0

    while !queue.empty?
      n, path = queue.shift
      raise NonTerminatingWalk, "dep_closure(#{name})" if (steps += 1) > limit
      check_cycle(n, path)
      next if seen.include?(n)
      seen.add(n)
      out << n
      (graph[n] || []).each { |d| queue << [d, path + [d]] }
    end

    return out
  end

  # Compute the install order for a set of requested packages.
  #
  # 1. BFS from `requested` to collect the transitive closure of deps.
  # 2. Remove anything in `installed`.
  # 3. Topological sort the remaining subgraph (Kahn's algorithm,
  #    with alphabetical tie-breaking for deterministic output).
  #
  # Returns an Array of package names, dependencies first.
  #
  # Raises MissingDepError if a requested name or any transitive dep
  # is not in the graph.
  def resolve(requested, graph, installed = [])

    installed = installed.to_a.to_set

    # --- 1. Transitive closure via BFS ---
    #
    # Stop traversal at installed packages: if a dep is already
    # installed, we trust that its own deps are satisfied (same
    # assumption APT makes). Only uninstalled packages and their
    # transitive deps are collected.
    needed = Set.new
    queue = requested.map { |r| [r, [r]] }
    limit = walk_limit(graph, requested.length)
    steps = 0

    while !queue.empty?
      name, path = queue.shift
      raise NonTerminatingWalk, "resolve(#{requested.join(', ')})" \
        if (steps += 1) > limit
      check_cycle(name, path)
      next if needed.include?(name)  # mutation: equivalent -- a Set adds once
      next if installed.include?(name)

      if !graph.key?(name)
        raise MissingDepError, "Unknown package: #{name}"
      end

      needed.add(name)
      graph[name].each { |dep|
        dep_path = path + [dep]
        check_cycle(dep, dep_path)          # before "already needed" hides it
        queue.push([dep, dep_path]) if !needed.include?(dep)
      }
    end

    # --- 3. Kahn's toposort on the subgraph ---
    #
    # We want install order: dependencies before dependents. A package
    # is "ready" (in-degree 0) when all its deps within `needed` have
    # already been placed. When a package is placed, everything that
    # depends on it has its in-degree decremented.
    #
    # in_degree[n] = number of n's deps that are still in `needed`.
    # rev[dep]     = list of packages that depend on dep.
    in_degree = {}
    rev = {}
    needed.each { |n| in_degree[n] = 0; rev[n] = [] }

    needed.each do |n|
      graph[n].each do |dep|
        next if !needed.include?(dep)
        in_degree[n] += 1
        rev[dep] << n
      end
    end

    # Seed the queue with zero-in-degree nodes (leaf deps), sorted
    # alphabetically for deterministic output.
    queue = in_degree.select { |_, d| d == 0 }.keys.sort
    result = []

    while !queue.empty?
      node = queue.shift
      result << node

      rev[node].each do |dependent|
        in_degree[dependent] -= 1
        if in_degree[dependent] == 0
          # Insert in sorted position to maintain alphabetical order.
          # mutation: equivalent -- the queue holds no duplicates
          idx = queue.bsearch_index { |x| x >= dependent } || queue.length
          queue.insert(idx, dependent)
        end
      end
    end

    # Safety check: if result doesn't cover all needed nodes, there's
    # a cycle in the subgraph (shouldn't happen if validate() ran first).
    if result.length != needed.length
      leftover = needed - result.to_set
      raise CycleError,
            "Cycle in subgraph: #{leftover.to_a.join(', ')}"
    end

    return result
  end
end
