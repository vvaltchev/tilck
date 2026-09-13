# SPDX-License-Identifier: BSD-2-Clause
#
# A PARSED RUBY FILE, for the two instruments that read the package
# manager's own source: the ambient-read lint (lint/ambient.rb) and
# the mutation operators (mutation/operators.rb). Both need what a
# grep cannot give -- a comment is not a read, a receiver is not a
# bare name, a site is a node with a byte range -- and both need it
# on every Ruby the package manager runs on, which includes the system
# 3.2 of Ubuntu 24.04 with no gem installed.
#
# Ripper is the standard library's parser and has been since 1.9. Its
# tree is FAITHFUL to the source -- a trailing `return nil` is in it,
# a `nil` in a ternary arm is in it -- which the interpreter's own
# RubyVM::AbstractSyntaxTree is not: that one folds both away before
# anyone can look, and a site that is not in the tree is a mutant
# nobody runs. What Ripper's tree lacks is positions on anything but
# the leaves, so the ranges are reconstructed here by walking the
# tree left to right with a cursor into the token stream: a
# parenthesised expression starts at the `(` the cursor meets next
# and ends at the `)` after its last child, a `def` at its keyword and
# its `end`, and so on for every kind that carries tokens the tree
# leaves out. A construct this does not know how to place raises,
# rather than guessing a range that a rewrite would then corrupt.
#
# Columns are BYTES, in Ripper and everywhere here: an em-dash in a
# comment must not move a range, and it once did, under a parser that
# reported bytes to code that indexed characters.
#
# Prism, which this replaces, is bundled only from Ruby 3.3. On a 3.2
# it is a gem with a C extension, and needing it here put ruby-dev on
# the bootstrap's package list for the sake of two test files.
#

require 'ripper'

class RubyTree

  class Error < StandardError; end

  # One node of the tree. Leaves carry their `text`; every placed node
  # carries a byte `range`. `op_range` is the operator token of a
  # binary node; `opener` is the type of the token a literal began
  # with, which is how `%w[ALL]` and `%i[all]` tell their words apart.
  class Node
    attr_reader :type, :children, :text
    attr_accessor :range, :op_range, :opener

    def initialize(type, children = [], text = nil)
      @type = type
      @children = children
      @text = text
    end

    def leaf? = !@text.nil?

    # Children that are nodes, in tree order.
    def nodes = @children.grep(Node)
  end

  attr_reader :root, :src

  def initialize(src)

    @src = src.b
    sexp = Ripper.sexp(utf8(src))
    raise Error, "syntax error" if sexp.nil?

    # Byte offset at which each line starts, index 0 = line 1.
    @line_start = [0]
    @src.each_line { |l| @line_start << @line_start.last + l.bytesize }

    @tokens = Ripper.lex(utf8(src)).map { |(l, c), type, tok, _state|
      [offset(l, c), type, tok.b]
    }
    @match = match_brackets

    @root = convert(sexp)
    place(@root, 0)
  end

  def offset(line, col) = @line_start[line - 1] + col
  def start_of(n) = n.range.begin
  def end_of(n) = n.range.end
  def range(n) = n.range
  def text(n) = @src[n.range]

  # The 1-based line a byte offset falls on.
  def line_of(at)
    return @line_start.bsearch_index { |s| s > at } || @line_start.length
  end

  # The line a node starts on, as a human reads it.
  def line_text(n) = utf8(@src.lines[line_of(n.range.begin) - 1].to_s).strip

  # Depth-first, with the chain of enclosing nodes, nearest first. A
  # node without a range -- the inside of a heredoc -- is not visited.
  def each_node(node = @root, ancestors = [], &blk)
    return if node.range.nil?
    blk.call(node, ancestors)
    inner = [node] + ancestors
    node.nodes.each { |c| each_node(c, inner, &blk) }
  end

  # --- from Ripper's arrays to nodes ------------------------------------

  private

  def utf8(s) = s.dup.force_encoding("UTF-8")

  # A leaf is [:@type, "text", [line, col]]; an inner node is
  # [:type, child...]; a bare Array is a list of statements or of
  # parameters. Symbols (a binary operator), booleans and nils are
  # kept as they are, so a kind's children stay at their indexes.
  def convert(x)

    return x if !x.is_a?(Array)
    return Node.new(:list, x.map { |e| convert(e) }) if !x[0].is_a?(Symbol)

    type = x[0]
    if type.to_s.start_with?("@")
      at = offset(*x[2])
      n = Node.new(type, [], x[1].b)
      n.range = (at...at + x[1].bytesize)
      return n
    end

    return convert_params(x) if type == :params
    return Node.new(type, x[1..].map { |e| convert(e) })
  end

  # [:params, req, opt, rest, post, kw, kwrest, block]: an optional
  # parameter is [name, default] and a keyword one [label, default or
  # false]. They get a kind of their own, because a default value is
  # an interface and not a decision, and the operators need to see
  # which values are defaults.
  def convert_params(x)
    kids = x[1..].each_with_index.map { |e, i|
      case i
      when 1 then e && Node.new(:list, e.map { |p| param(:opt_param, p) })
      when 4 then e && Node.new(:list, e.map { |p| param(:kw_param, p) })
      else convert(e)
      end
    }
    return Node.new(:params, kids)
  end

  def param(kind, pair)
    name, default = pair
    return Node.new(kind, [convert(name), default ? convert(default) : nil])
  end

  # --- ranges, from a left-to-right walk over the tokens ----------------

  # Kinds that open with a keyword and close with `end`.
  KEYWORD_BLOCKS = {
    if: "if", unless: "unless", while: "while", until: "until",
    case: "case", begin: "begin", class: "class", module: "module",
    sclass: "class", for: "for", do_block: "do",
  }.freeze

  # Kinds whose first token is a keyword and which end with their
  # last child (or with the keyword, when there is none).
  KEYWORD_HEADS = {
    return: "return", return0: "return", next: "next", break: "break",
    yield: "yield", yield0: "yield", super: "super", zsuper: "super",
    redo: "redo", retry: "retry", defined: "defined?", alias: "alias",
    undef: "undef", rescue: "rescue", ensure: "ensure", else: "else",
    elsif: "elsif", when: "when", in: "in", BEGIN: "BEGIN", END: "END",
  }.freeze

  # Kinds whose children are not in source order.
  REORDERED = %i[if_mod unless_mod while_mod until_mod].freeze

  LITERALS = {
    string_literal:  [%i[on_tstring_beg on_heredoc_beg], %i[on_tstring_end]],
    xstring_literal: [%i[on_backtick], %i[on_tstring_end]],
    regexp_literal:  [%i[on_regexp_beg], []],
    dyna_symbol:     [%i[on_symbeg on_tstring_beg],
                      %i[on_tstring_end on_label_end]],
    symbol_literal:  [%i[on_symbeg], []],
    hash:            [%i[on_lbrace], %i[on_rbrace]],
    brace_block:     [%i[on_lbrace], %i[on_rbrace]],
    array:           [%i[on_lbracket on_qwords_beg on_words_beg
                         on_qsymbols_beg on_symbols_beg],
                      %i[on_rbracket on_tstring_end]],
    paren:           [%i[on_lparen], %i[on_rparen]],
    arg_paren:       [%i[on_lparen], %i[on_rparen]],
    string_embexpr:  [%i[on_embexpr_beg], %i[on_embexpr_end]],
  }.freeze

  # Place `node` at or after byte `cursor`; return the cursor after it.
  def place(node, cursor)

    return place_leaf(node, cursor) if node.leaf?

    type = node.type
    if (kw = KEYWORD_BLOCKS[type]) && (open = opener(node, cursor, texts: [kw]))
      c = place_all(node.nodes, open.end)
      close = token(c, texts: ["end"])
      return finish(node, open.begin, close.end)
    end

    if (kw = KEYWORD_HEADS[type])
      open = token(cursor, texts: [kw])
      c = place_all(node.nodes, open.end)
      return finish(node, open.begin, c)
    end

    if (pair = LITERALS[type]) && (open = opener(node, cursor, types: pair[0]))
      return place_literal(node, open, *pair)
    end

    case type
    when :def, :defs then return place_def(node, cursor)
    when :binary     then return place_binary(node, cursor)
    when :unary      then return place_unary(node, cursor)
    when :lambda     then return place_lambda(node, cursor)
    when :aref, :aref_field
      recv, args = node.nodes
      c = place(recv, cursor)
      open = token(c, types: %i[on_lbracket])
      c = args ? place(args, open.end) : open.end
      close = token(c, types: %i[on_rbracket])
      return finish(node, recv.range.begin, close.end)
    when :void_stmt
      return finish(node, cursor, cursor)
    end

    kids = node.nodes
    kids = [kids[1], kids[0]] if REORDERED.include?(type)
    c = place_all(kids, cursor)
    first = kids.compact.map { |k| k.range&.begin }.compact.min
    return finish(node, first || cursor, c)
  end

  def place_all(nodes, cursor)
    nodes.each { |n| cursor = place(n, cursor) }
    return cursor
  end

  # A node ends where its last token does -- and the tree leaves some
  # tokens out entirely: `defined?(x)` has no node for its parentheses
  # and neither has `(1..9)` in a pattern. Any closing bracket that
  # follows a node and pairs with an opener INSIDE it is the node's,
  # because brackets nest, so it is taken in.
  def finish(node, from, to)

    while (t = peek(to)) && (open_at = @match[t[0]]) && open_at >= from
      to = t[0] + t[2].bytesize
    end

    node.range = (from...to)
    return to
  end

  def place_leaf(node, cursor)
    if node.range.begin < cursor
      raise Error, "#{node.type} #{node.text.inspect} at " \
                   "#{node.range.begin} lies before the cursor #{cursor}"
    end
    return node.range.end
  end

  # A literal opens with one of `opens` and, when `closes` is not
  # empty, closes with one of those after its last child. A heredoc
  # is its opening token alone -- the body sits lines further down,
  # among other statements' tokens -- and its inside is not placed.
  def place_literal(node, open, _opens, closes)

    node.opener = @tokens[open_index(open.begin)][1]

    if node.opener == :on_heredoc_beg
      return finish(node, open.begin, open.end)
    end

    c = place_all(node.nodes, open.end)
    return finish(node, open.begin, c) if closes.empty?

    close = token(c, types: closes)
    return finish(node, open.begin, close.end)
  end

  # [:def, name, params, body] and [:defs, recv, period, name, params,
  # body]. The body of an endless def follows an `=`, and there is no
  # `end`.
  def place_def(node, cursor)

    open = token(cursor, texts: ["def"])
    *head, body = node.nodes
    c = place_all(head, open.end)

    nxt = peek(c)
    if nxt && nxt[1] == :on_op && nxt[2] == "="
      c = place(body, nxt[0] + 1)
      return finish(node, open.begin, c)
    end

    c = place(body, c)
    close = token(c, texts: ["end"])
    return finish(node, open.begin, close.end)
  end

  # [:binary, left, op, right]: the operator token is recorded, since
  # it is what a flipped comparison rewrites.
  def place_binary(node, cursor)
    left, op, right = node.children
    c = place(left, cursor)
    node.op_range = token(c, texts: [op.to_s])
    c = place(right, node.op_range.end)
    return finish(node, left.range.begin, c)
  end

  # [:unary, op, operand]: `!x`, `not x`, `-x`.
  def place_unary(node, cursor)
    op, operand = node.children
    open = token(cursor, texts: [op.to_s.delete_suffix("@")])
    c = place(operand, open.end)
    return finish(node, open.begin, c)
  end

  # [:lambda, params, body]: `->`, the parameters, then a `{ }` or a
  # `do end` around the body.
  def place_lambda(node, cursor)
    params, body = node.nodes
    open = token(cursor, types: %i[on_tlambda])
    c = params ? place(params, open.end) : open.end
    brace = token(c, types: %i[on_tlambeg on_kw])
    c = place(body, brace.end)
    close = token(c, texts: [@tokens[open_index(brace.begin)][2] == "{" ?
                             "}" : "end"])
    return finish(node, open.begin, close.end)
  end

  # The token `node` opens with, if one of the given kind lies between
  # the cursor and the node's first leaf; nil when the node has none.
  #
  # Between two placed nodes the tree leaves tokens out -- a comma, a
  # ternary's `?` -- so the opener is not necessarily the NEXT token.
  # And a kind does not always have one: `x in pattern` parses to a
  # :case with no keyword and no `end`, and a symbol under `alias`
  # has no colon. Such a node is placed from its children instead.
  def opener(node, cursor, types: nil, texts: nil)
    r = token(cursor, types: types, texts: texts, or_nil: true)
    return nil if r.nil?
    limit = first_leaf_offset(node)
    return limit && r.begin > limit ? nil : r
  end

  def first_leaf_offset(node)
    return node.range.begin if node.leaf?
    node.nodes.each { |c|
      at = first_leaf_offset(c)
      return at if at
    }
    return nil
  end

  # --- the token stream -------------------------------------------------

  OPENERS = %i[on_lparen on_lbracket on_lbrace on_tlambeg
               on_embexpr_beg].freeze
  CLOSERS = %i[on_rparen on_rbracket on_rbrace on_embexpr_end].freeze

  # Offset of the opening bracket each closing one pairs with. The
  # lexer tells the kinds apart, so a plain stack is exact.
  def match_brackets
    stack = []
    out = {}
    @tokens.each { |at, type, _tok|
      if OPENERS.include?(type)
        stack.push(at)
      elsif CLOSERS.include?(type)
        out[at] = stack.pop if !stack.empty?
      end
    }
    return out
  end

  # Tokens that are not code: a text match never means one of these.
  # Comments and string contents can spell anything, `end` included.
  PROSE = %i[on_sp on_nl on_ignored_nl on_comment on_embdoc_beg
             on_embdoc on_embdoc_end on_tstring_content on_heredoc_end
             on_words_sep on___end__].freeze

  # Index of the first token at or after byte `at`.
  def next_index(at)
    return @tokens.bsearch_index { |t| t[0] >= at } || @tokens.length
  end

  def open_index(at) = @tokens.bsearch_index { |t| t[0] >= at }

  # The first code token at or after `at`, or nil.
  def peek(at)
    i = next_index(at)
    i += 1 while i < @tokens.length && PROSE.include?(@tokens[i][1])
    return @tokens[i]
  end

  # The byte range of the first token at or after `cursor` matching
  # the given types or texts.
  def token(cursor, types: nil, texts: nil, or_nil: false)

    i = next_index(cursor)
    while i < @tokens.length
      at, type, tok = @tokens[i]
      if (types && types.include?(type)) ||
         (texts && !PROSE.include?(type) && texts.include?(tok))
        return (at...at + tok.bytesize)
      end
      i += 1
    end

    return nil if or_nil
    want = types ? types.join("/") : texts.join("/")
    raise Error, "no #{want} at or after byte #{cursor} " \
                 "(line #{line_of(cursor)})"
  end
end
