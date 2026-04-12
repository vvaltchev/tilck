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

      for dep in (graph[node] || [])
        if color[dep] == gray
          cycle_start = path.index(dep)
          cycle = path[cycle_start..] + [dep]
          raise CycleError,
                "Dependency cycle: #{cycle.join(' -> ')}"
        end
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
    queue = requested.dup

    while !queue.empty?
      name = queue.shift
      next if needed.include?(name)
      next if installed.include?(name)

      if !graph.key?(name)
        raise MissingDepError, "Unknown package: #{name}"
      end

      needed.add(name)
      graph[name].each { |dep| queue.push(dep) if !needed.include?(dep) }
    end

    return [] if needed.empty?

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
