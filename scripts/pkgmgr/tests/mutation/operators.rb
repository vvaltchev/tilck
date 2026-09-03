# SPDX-License-Identifier: BSD-2-Clause
#
# MUTATION OPERATORS: the ways a line of the logic core can be wrong.
#
# Line coverage says a line ran. It cannot say whether any test would
# have noticed had the line been wrong, and every bug this package
# manager has had was on a line that ran, under a test that passed.
# The question worth asking is the second one, and the way to ask it
# is to make the line wrong on purpose and run the suite: a mutant the
# suite kills is a line the tests defend; a mutant that survives is a
# test that does not exist, named to the line.
#
# The operators here are not generic. Each is a bug this tree has
# actually had, or its nearest neighbour:
#
#   O1  a comparison flipped              ==/!=  </<=  >/>=
#   O2  a conjunction flipped             && <-> ||
#   O3  a negation dropped                !x -> x
#   O4  one conjunct dropped              a && b -> a, -> b
#   O5  a guard deleted                   `return x if c` / `next if c`
#   O6  a fallback dropped                a || b -> a
#   O7  "all" and "nothing" swapped       nil <-> "ALL", :all <-> nil
#   O8  a filter inverted                 select/reject, any?/all?
#   O9  a scope not opened                with_target_coords(a, b) ->
#                                         with_target_arch(a); board_for(a)
#                                         -> BOARD; target_arch -> ARCH;
#                                         pkg_dirname -> name;
#                                         coords(v) -> coords()
#   O10 a scope not restored              @x = prev -> @x = nil
#
# Row 4 of the bug table (a recipe judged at the wrong stack) is O9;
# row 2 (nil where "ALL" was meant) is O7; row 3 (an arch matched as
# a whole coordinate) is O4.
#
# Prism, like the digests and the lint: a site is a node, the mutant
# is a byte-range rewrite of the original source, and nothing is
# matched by regex.
#

require 'prism'

module Mutation

  Site = Struct.new(:file, :line, :op, :from, :to, :range) do
    def id = "#{File.basename(file)}:#{line}:#{op}:#{range.begin}"
    def to_s
      "#{File.basename(file)}:#{line}@#{range.begin}  #{op}  #{from} -> #{to}"
    end
  end

  RELATIONAL = { :== => "!=", :!= => "==", :< => "<=", :<= => "<",
                 :> => ">=", :>= => ">" }.freeze

  FILTERS = { select: "reject", reject: "select", any?: "all?",
              all?: "any?", find: "reject" }.freeze

  # Bare calls whose replacement reads the ambient state instead.
  AMBIENT = { target_arch: "ARCH", board_for: "BOARD",
              pkg_dirname: "name" }.freeze

  module_function

  # Every site in `src`, restricted to the byte ranges in `within`
  # (nil = the whole file).
  def sites(file, src, within: nil)

    result = Prism.parse(src)
    out = []

    walk(result.value) { |node, parent|
      at = node.location.start_offset
      next if within && !within.any? { |r| r.cover?(at) }
      mutate(node, parent, file, src, out)
    }

    return out
  end

  def walk(node, parent = nil, &blk)
    return if node.nil?
    blk.call(node, parent)
    node.compact_child_nodes.each { |c| walk(c, node, &blk) }
  end

  # A default parameter value is an interface, not a decision: `ver =
  # nil` says the caller may omit it. Rewriting it to "ALL" changes
  # what callers that omit it get, which no test of THIS file can be
  # expected to pin; the callers' tests do. Not a site.
  DEFAULTS = [Prism::OptionalParameterNode,
              Prism::OptionalKeywordParameterNode].freeze

  def default_value?(parent) = DEFAULTS.any? { |k| parent.is_a?(k) }

  def loc_range(loc) = (loc.start_offset...loc.end_offset)

  def add(out, file, src, node_or_loc, op, to)
    loc = node_or_loc.respond_to?(:location) ? node_or_loc.location
                                              : node_or_loc
    r = loc_range(loc)
    from = src[r]
    return if from == to
    out << Site.new(file, loc.start_line, op, from, to, r)
  end

  def mutate(node, parent, file, src, out)

    case node
    when Prism::CallNode
      name = node.name

      # O1: the operator token only, so `a == b` stays `a != b`.
      if RELATIONAL.key?(name) && node.message_loc
        add(out, file, src, node.message_loc, "O1", RELATIONAL[name])
      end

      # O3
      if name == :! && node.receiver && node.message_loc
        add(out, file, src, node, "O3", src[loc_range(node.receiver.location)])
      end

      # O8
      if FILTERS.key?(name) && node.message_loc
        add(out, file, src, node.message_loc, "O8", FILTERS[name].to_s)
      end

      # O9: bare reads of the scope, and the two scope openers.
      if AMBIENT.key?(name) && node.receiver.nil?
        if name == :board_for || name == :target_arch
          add(out, file, src, node, "O9", AMBIENT[name]) if node.block.nil?
        else
          add(out, file, src, node, "O9", AMBIENT[name])
        end
      end

      if name == :with_target_coords && node.arguments &&
         node.arguments.arguments.length == 2
        first = src[loc_range(node.arguments.arguments[0].location)]
        blk = node.block ? " " + src[loc_range(node.block.location)] : ""
        add(out, file, src, node, "O9", "with_target_arch(#{first})#{blk}")
      end

      if name == :coords && node.receiver.nil? && node.arguments &&
         node.arguments.arguments.length == 1
        add(out, file, src, node, "O9", "coords()")
      end

    when Prism::AndNode
      add(out, file, src, node.operator_loc, "O2", "||")
      add(out, file, src, node, "O4", src[loc_range(node.left.location)])
      add(out, file, src, node, "O4", src[loc_range(node.right.location)])

    when Prism::OrNode
      add(out, file, src, node.operator_loc, "O2", "&&")
      add(out, file, src, node, "O6", src[loc_range(node.left.location)])

    when Prism::IfNode, Prism::UnlessNode
      # O5: a modifier guard whose whole body is one return/next.
      body = node.statements&.body
      if body && body.length == 1 &&
         (body[0].is_a?(Prism::ReturnNode) || body[0].is_a?(Prism::NextNode)) &&
         node.end_keyword_loc.nil?
        add(out, file, src, node, "O5", "nil")
      end

    when Prism::NilNode
      add(out, file, src, node, "O7", '"ALL"') if !default_value?(parent)

    when Prism::StringNode
      if node.unescaped == "ALL" && !default_value?(parent)
        add(out, file, src, node, "O7", "nil")
      end

    when Prism::SymbolNode
      if node.unescaped == "all" && !default_value?(parent)
        add(out, file, src, node, "O7", "nil")
      end

    when Prism::InstanceVariableWriteNode
      v = node.value
      if v.is_a?(Prism::LocalVariableReadNode) && v.name == :prev
        add(out, file, src, node, "O10", "#{node.name} = nil")
      end
    end
  end

  # The source with one site rewritten.
  def apply(src, site)
    out = src.dup
    out[site.range] = site.to
    return out
  end

  # Byte ranges of the named methods in a file, for files where only
  # some methods are in scope.
  def method_ranges(src, names)

    result = Prism.parse(src)
    want = names.map(&:to_s).to_set
    out = []

    walk(result.value) { |n, _|
      next if !n.is_a?(Prism::DefNode)
      out << loc_range(n.location) if want.include?(n.name.to_s)
    }

    return out
  end

  # Lines carrying `# mutation: equivalent -- <reason>`: sites on them
  # are skipped, and the annotation is checked to still sit on a site.
  def equivalent_lines(src)
    out = {}
    src.each_line.with_index(1) { |l, i|
      m = l.match(/#\s*mutation:\s*equivalent\s*--\s*(.+)$/)
      out[i] = m[1].strip if m
    }
    return out
  end
end
