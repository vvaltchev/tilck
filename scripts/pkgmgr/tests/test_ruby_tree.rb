# SPDX-License-Identifier: BSD-2-Clause
#
# The parse tree the lint and the mutation operators read.
#
# Both instruments rewrite or report BYTE RANGES of the source, and a
# range that is off by a token is a mutant that does not parse or a
# violation on the wrong line. Ripper places only the leaves; every
# other range is reconstructed (ruby_tree.rb), and each rule of that
# reconstruction has a case here that would have caught it wrong.
#

require_relative 'test_helper'
require_relative 'ruby_tree'

class TestRubyTree < Minitest::Test

  include TestHelper
  include SourceAudit

  PKGMGR = File.expand_path("..", __dir__)

  def tree(src) = RubyTree.new(src)

  # All nodes of a type, in tree order.
  def all(t, type)
    out = []
    t.each_node { |n, _| out << n if n.type == type }
    return out
  end

  def texts(t, type) = all(t, type).map { |n| t.text(n) }

  # --- why Ripper -------------------------------------------------------

  # The interpreter's own tree (RubyVM::AbstractSyntaxTree) folds a
  # trailing `return nil` and a `nil` in a ternary arm away before
  # anyone can look. A site that is not in the tree is a mutant nobody
  # runs, so the tree has to be the syntactic one.
  def test_a_trailing_return_nil_and_a_nil_arm_are_in_the_tree
    t = tree("def f(c)\n  x = c ? nil : 1\n  return nil\nend\n")
    nils = all(t, :var_ref).select { |n| t.text(n) == "nil" }
    assert_equal 2, nils.length
    assert_equal ["return nil"], texts(t, :return)
  end

  # --- bytes, not characters --------------------------------------------

  def test_ranges_are_bytes_after_multibyte_text
    t = tree("# é — ü\nx = (a && b)\n")
    assert_equal ["(a && b)"], texts(t, :paren)
    b = all(t, :binary).first
    assert_equal "&&", t.src[b.op_range]
    assert_equal 2, t.line_of(b.range.begin)
  end

  # --- tokens the tree leaves out ---------------------------------------

  # A ternary's `?` and `:`, and a comma, sit between two placed nodes
  # and belong to neither; the literal after them still starts at its
  # own quote.
  def test_literals_after_unrepresented_tokens_keep_their_delimiters
    t = tree("x = c ? \"ALL\" : :all\nf(a, \"s\", :sym)\n")
    assert_equal ['"ALL"', '"s"'], texts(t, :string_literal)
    assert_equal [":all", ":sym"], texts(t, :symbol_literal)
  end

  # `defined?(x)` has no node for its parentheses and neither has a
  # parenthesised pattern: a closing bracket pairing with an opener
  # inside the node is the node's.
  def test_closing_brackets_pairing_inside_a_node_are_taken_in
    t = tree("a = !defined?(x)\nok = (y in (1..2) and z)\n")
    assert_equal ["!defined?(x)"], texts(t, :unary)
    assert_equal ["y in (1..2)"], texts(t, :case)
  end

  # A block's `|e|` is not closed by any node; the parenthesis after it
  # is still found, and the block still spans its braces.
  def test_block_parameters_do_not_hide_the_next_opener
    t = tree("l.map { |e| (e) }\n")
    assert_equal ["(e)"], texts(t, :paren)
    assert_equal ["{ |e| (e) }"], texts(t, :brace_block)
  end

  # --- kinds that do not always have their keyword ----------------------

  def test_an_endless_def_ends_with_its_body_and_the_next_def_is_whole
    t = tree("def a(v = nil) = [\n  1,\n]\ndef <=(o) = 1\ndef b\n  2\nend\n")
    assert_equal ["def a(v = nil) = [\n  1,\n]", "def <=(o) = 1",
                  "def b\n  2\nend"], texts(t, :def)
    nil_default = all(t, :opt_param).first.children[1]
    assert_equal "nil", t.text(nil_default)
  end

  def test_a_symbol_under_alias_has_no_colon_and_a_pattern_no_case
    t = tree("alias eql? ==\ncase x\nwhen 1 then 2\nend\n")
    assert_equal ["eql?", "=="], texts(t, :symbol_literal)
    assert_equal ["case x\nwhen 1 then 2\nend"], texts(t, :case)
  end

  # --- heredocs ---------------------------------------------------------

  # The body sits lines further down, among other statements' tokens.
  # The literal is its opening token, the call around it closes on the
  # same line, and the statement after the body is placed.
  def test_a_heredoc_is_its_opening_token
    t = tree("f(x, <<~T)\n  body\nT\ny = 1\n")
    assert_equal ["<<~T"], texts(t, :string_literal)
    assert_equal ["(x, <<~T)"], texts(t, :arg_paren)
    assert_equal ["y = 1"], texts(t, :assign)
  end

  # --- what the operators read off a node -------------------------------

  def test_the_operator_of_a_binary_is_the_token_after_its_left_side
    t = tree("(a == b) == c\n")
    outer, inner = all(t, :binary)
    assert_equal "(a == b) == c", t.text(outer)
    assert_equal 9, outer.op_range.begin
    assert_equal 3, inner.op_range.begin
  end

  def test_word_and_symbol_arrays_remember_their_opener
    t = tree("%w[ALL] + %i[all] + [1]\n")
    assert_equal %i[on_qwords_beg on_qsymbols_beg on_lbracket],
                 all(t, :array).map(&:opener)
  end

  def test_lambdas_and_do_blocks
    t = tree("a = ->(x) { x }\nb = -> do 1 end\nl.each do |e| e end\n")
    assert_equal ["->(x) { x }", "-> do 1 end"], texts(t, :lambda)
    assert_equal ["do |e| e end"], texts(t, :do_block)
  end

  # --- it says when it does not know ------------------------------------

  def test_a_syntax_error_raises
    assert_raises(RubyTree::Error) { tree("def (\n") }
  end

  # --- invariants, on the real sources ----------------------------------

  # Every child lies within its parent, and siblings do not overlap:
  # what a placement that grabbed the wrong closer would break.
  def test_children_lie_within_their_parents_in_the_real_sources
    for f in %w[package_manager.rb main.rb recipe.rb] do
      t = tree(File.binread(File.join(PKGMGR, f)))
      t.each_node { |n, up|
        if (p = up[0])
          assert p.range.begin <= n.range.begin &&
                 n.range.end <= p.range.end,
                 "#{f}: #{n.type} #{n.range} outside #{p.type} #{p.range}"
        end
        kids = n.nodes.select { |k| k.range }.sort_by { |k| k.range.begin }
        kids.each_cons(2) { |a, b|
          assert a.range.end <= b.range.begin,
                 "#{f}: #{a.type} #{a.range} overlaps #{b.type} #{b.range}"
        }
      }
    end
  end
end
