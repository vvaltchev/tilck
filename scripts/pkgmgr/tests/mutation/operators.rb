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
# A parse tree, like the lint: a site is a node, the mutant is a
# byte-range rewrite of the original source, and nothing is matched
# by regex. See ruby_tree.rb for the parser.
#

require_relative '../ruby_tree'

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

    tree = RubyTree.new(src)
    out = []

    tree.each_node { |node, up|
      at = tree.start_of(node)
      next if within && !within.any? { |r| r.cover?(at) }
      mutate(tree, node, up, file, out)
    }

    return out
  end

  # A default parameter value is an interface, not a decision: `ver =
  # nil` says the caller may omit it. Rewriting it to "ALL" changes
  # what callers that omit it get, which no test of THIS file can be
  # expected to pin; the callers' tests do. Not a site.
  def default_value?(up) = %i[opt_param kw_param].include?(up[0]&.type)

  # --- calls, as Ripper splits them ------------------------------------
  #
  # `f(a) { }` is three nodes: fcall inside method_add_arg inside
  # method_add_block. The name sits on the innermost, and "the call"
  # a rewrite replaces is the outermost.

  CALL_KINDS = %i[call command_call vcall fcall command].freeze
  WRAPPERS = %i[method_add_arg method_add_block].freeze

  def leaf?(x) = x.is_a?(RubyTree::Node) && x.leaf?

  # The name leaf of a call node.
  def name_leaf(node)
    case node.type
    when :call, :command_call then node.children[2]
    else node.children[0]
    end
  end

  def call_name(node)
    leaf = name_leaf(node)
    return leaf?(leaf) ? leaf.text.to_sym : nil
  end

  def receiver(node)
    return %i[call command_call].include?(node.type) ? node.children[0] : nil
  end

  # The outermost node of the call `node` names.
  def unit_of(node, up)
    top = node
    up.each { |p|
      break if !WRAPPERS.include?(p.type) || !p.children[0].equal?(top)
      top = p
    }
    return top
  end

  # The argument nodes of a call unit.
  def args_of(x)

    return [] if !x.is_a?(RubyTree::Node)

    case x.type
    when :method_add_block          then args_of(x.children[0])
    when :method_add_arg            then args_of(x.children[1])
    when :command                   then args_of(x.children[1])
    when :command_call              then args_of(x.children[3])
    when :arg_paren, :args_add_block then args_of(x.children[0])
    when :list                      then x.nodes
    else []
    end
  end

  # The `&blk` argument of a call unit, if any.
  def block_arg_of(x)

    return nil if !x.is_a?(RubyTree::Node)

    case x.type
    when :method_add_block          then block_arg_of(x.children[0])
    when :method_add_arg            then block_arg_of(x.children[1])
    when :command                   then block_arg_of(x.children[1])
    when :command_call              then block_arg_of(x.children[3])
    when :arg_paren                 then block_arg_of(x.children[0])
    when :args_add_block
      b = x.children[1]
      return b.is_a?(RubyTree::Node) ? b : nil
    else nil
    end
  end

  def add(out, file, tree, range, op, to)
    from = tree.src[range]
    return if from == to
    out << Site.new(file, tree.line_of(range.begin), op, from, to, range)
  end

  def mutate(tree, node, up, file, out)

    case node.type
    when :binary
      left, op, right = node.children

      # O1: the operator token only, so `a == b` stays `a != b`.
      if RELATIONAL.key?(op)
        add(out, file, tree, node.op_range, "O1", RELATIONAL[op])
      end

      if op == :"&&" || op == :and
        add(out, file, tree, node.op_range, "O2", "||")
        add(out, file, tree, node.range, "O4", tree.text(left))
        add(out, file, tree, node.range, "O4", tree.text(right))
      end

      if op == :"||" || op == :or
        add(out, file, tree, node.op_range, "O2", "&&")
        add(out, file, tree, node.range, "O6", tree.text(left))
      end

    when :unary
      # O3
      op, operand = node.children
      if op == :! || op == :not
        add(out, file, tree, node.range, "O3", tree.text(operand))
      end

    when *CALL_KINDS
      name = call_name(node)
      recv = receiver(node)
      unit = unit_of(node, up)
      args = args_of(unit)
      blocked = unit.type == :method_add_block

      # O8
      if FILTERS.key?(name)
        add(out, file, tree, name_leaf(node).range, "O8", FILTERS[name])
      end

      # O9: bare reads of the scope, and the two scope openers.
      if AMBIENT.key?(name) && recv.nil?
        if name == :board_for || name == :target_arch
          add(out, file, tree, unit.range, "O9", AMBIENT[name]) if !blocked
        else
          add(out, file, tree, unit.range, "O9", AMBIENT[name])
        end
      end

      # The receiver, if any, goes with it: the mutant is the call
      # rewritten to the narrower opener, whoever it was sent to.
      if name == :with_target_coords && args.length == 2
        first = tree.text(args[0])
        blk = blocked ? " " + tree.text(unit.children[1]) : ""
        blk = " &" + tree.text(block_arg_of(unit)) if block_arg_of(unit)
        add(out, file, tree, unit.range, "O9",
            "with_target_arch(#{first})#{blk}")
      end

      if name == :coords && recv.nil? && args.length == 1
        add(out, file, tree, unit.range, "O9", "coords()")
      end

    when :if_mod, :unless_mod
      # O5: a modifier guard whose whole body is one return/next.
      _cond, stmt = node.children
      if %i[return return0 next].include?(stmt.type)
        add(out, file, tree, node.range, "O5", "nil")
      end

    when :var_ref
      leaf = node.children[0]
      if leaf.type == :@kw && leaf.text == "nil" && !default_value?(up)
        add(out, file, tree, node.range, "O7", '"ALL"')
      end

    when :string_literal
      parts = node.children[0].nodes
      if parts.length == 1 && parts[0].type == :@tstring_content &&
         parts[0].text == "ALL" && !default_value?(up)
        add(out, file, tree, node.range, "O7", "nil")
      end

    when :symbol_literal
      leaf = node.children[0].children[0]
      if leaf?(leaf) && leaf.text == "all" && !default_value?(up)
        add(out, file, tree, node.range, "O7", "nil")
      end

    when :@tstring_content
      # A word of %w[] is a string and one of %i[] a symbol.
      words = up[1]&.type == :array ? up[1].opener : nil
      if %i[on_qwords_beg on_words_beg].include?(words) && node.text == "ALL"
        add(out, file, tree, node.range, "O7", "nil")
      end
      if %i[on_qsymbols_beg on_symbols_beg].include?(words) &&
         node.text == "all"
        add(out, file, tree, node.range, "O7", "nil")
      end

    when :assign
      # O10: `@x = prev` restores a scope; nothing else does.
      target, value = node.children
      ivar = target.type == :var_field ? target.children[0] : nil
      if ivar && ivar.type == :@ivar && value.type == :var_ref &&
         value.children[0].type == :@ident && value.children[0].text == "prev"
        add(out, file, tree, node.range, "O10", "#{ivar.text} = nil")
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

    tree = RubyTree.new(src)
    want = names.map(&:to_s).to_set
    out = []

    tree.each_node { |n, _|
      next if !%i[def defs].include?(n.type)
      name = n.type == :def ? n.children[0].text : n.children[2].text
      out << n.range if want.include?(name)
    }

    return out
  end

  # Lines excused by `# mutation: equivalent -- <reason>`: on the line
  # itself when it shares the line with code, or on the line BELOW
  # when the annotation stands alone (which is what keeps a long line
  # under eighty columns). Sites on an excused line are skipped, and
  # check-annotations requires each to still sit on a site.
  def equivalent_lines(src)
    out = {}
    src.each_line.with_index(1) { |l, i|
      m = l.match(/#\s*mutation:\s*equivalent\s*--\s*(.+)$/)
      next if !m
      alone = l.strip.start_with?("#")
      out[alone ? i + 1 : i] = m[1].strip
    }
    return out
  end
end
