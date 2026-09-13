# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT THE PACKAGE MANAGER IS ALLOWED TO READ.
#
# Every logic bug this tree has had was one bug: a question about a
# specific installation, answered from ambient state -- the global
# ARCH, the global BOARD, the stack that happened to be current --
# instead of from that installation's own coordinates. Six spellings
# of it were fixed one at a time, each found by a real command doing
# the wrong thing, because nothing said where the next one was.
#
# This is the thing that says. It parses every file of the package
# manager and reports:
#
#   R1  a read of ARCH, BOARD, DEFAULT_BOARD or HOST_VER_GCC anywhere
#       but the definitions, the CLI boundary, and the two accessors
#       that own the answer (PackageManager#target_arch, #board_for,
#       #current_host_stack);
#
#   R2  an identity comparison on a PART of a coordinate --
#       `e.arch == arch`, `x.compiler == cc` -- outside the value
#       object that IS the identity. An arch is two thirds of a
#       coordinate; matching on it took both boards of riscv64.
#
#
# There was a third rule, R3, on writes to the scope variables outside
# the methods that opened and closed them. A scope is a value now
# (scope.rb): nothing holds one, so nothing can leave one open.
#
# A parse tree, not regexes: a string "ARCH=x86" in a make invocation
# is not a read, a comment is not a read, and a receiver is not a bare
# name. See ruby_tree.rb for the parser and why it is the one it is.
#
# It is a test, so a new reader fails the suite the day it is written,
# with file, method and line. An allowlist entry needs a reason, and
# the test that the entry still names a real method is what stops the
# list from rotting.
#

require 'pathname'
require_relative '../ruby_tree'

module AmbientLint

  AMBIENT      = %i[ARCH BOARD DEFAULT_BOARD HOST_VER_GCC
                    HOST_OS_ARCH HOST_DISTRO HOST_CC].freeze
  COMPARISONS  = %i[== != eql?].freeze

  # `arch` and `compiler` are each a part of a coordinate, and an
  # InstallInfo still carries them. `target_arch` is not one: it is
  # what a cross compiler produces code FOR, metadata a compiler
  # install has and nothing else does, so selecting a compiler by it
  # is the right question rather than a partial one.
  PARTIAL_KEYS = %i[arch compiler].freeze

  # The node types a method call comes as, in Ripper's tree: with a
  # receiver (call, command_call), bare (vcall), with arguments and
  # no parentheses (command), or one of those wrapped in the node that
  # adds the parenthesised arguments or the block.
  CALLS        = %i[call command_call vcall fcall command
                    method_add_arg method_add_block].freeze
  DEFS         = %i[def defs].freeze

  Violation = Struct.new(:rule, :file, :method, :line, :text) do
    def where = "#{file}##{method || "<top>"}"
    def to_s = "#{rule}  #{file}:#{line}  #{where}  #{text}"
  end

  module_function

  # Every *.rb of the package manager proper: the tests are not the
  # subject, and a test may hold a planted violation on purpose.
  def sources(dir)
    return Pathname.glob(dir / "*.rb").sort
  end

  def scan_dir(dir)
    return sources(dir).flat_map { |f| scan_file(f) }
  end

  def scan_file(path)
    src = File.binread(path.to_s)
    return scan_source(src, file: Pathname(path).basename.to_s)
  end

  def scan_source(src, file: "<string>")
    tree = RubyTree.new(src)
    out = []
    tree.each_node { |node, up| check(tree, node, up, file, out) }
    return out
  end

  def call_name(n)
    case n.type
    when :call, :command_call         then leaf_sym(n.children[2])
    when :vcall, :fcall, :command     then leaf_sym(n.children[0])
    when :method_add_arg, :method_add_block then call_name(n.children[0])
    end
  end

  def leaf_sym(x) = x.is_a?(RubyTree::Node) && x.leaf? ? x.text.to_sym : nil

  # [:def, name, ...] and [:defs, receiver, period, name, ...].
  def def_name(d)
    return d.children[0].text if d.type == :def
    return "self.#{d.children[2].text}"
  end

  def call?(x) = x.is_a?(RubyTree::Node) && CALLS.include?(x.type)

  # `up` is the chain of enclosing nodes, nearest first. The method a
  # violation is reported in is the nearest def on it; a
  # module_function module (Main, Layout) and a class body look the
  # same to this, and `def self.x` is "self.x".
  def check(tree, node, up, file, out)

    found = ->(rule) {
      scope = up.find { |n| DEFS.include?(n.type) }
      out << Violation.new(rule, file, scope && def_name(scope),
                           tree.line_of(node.range.begin),
                           tree.line_text(node))
    }

    case node.type
    when :var_ref
      leaf = node.children[0]
      if leaf.type == :@const && AMBIENT.include?(leaf.text.to_sym)
        found.call(:R1)
      end

    when :binary
      left, op, _right = node.children
      if COMPARISONS.include?(op) && call?(left) &&
         PARTIAL_KEYS.include?(call_name(left))
        found.call(:R2)
      end

    when :call, :command_call
      recv = node.children[0]
      if COMPARISONS.include?(call_name(node)) && call?(recv) &&
         PARTIAL_KEYS.include?(call_name(recv))
        found.call(:R2)
      end

    end
  end

  # The methods a file defines, as "name" / "self.name" -- what an
  # allowlist entry has to still point at.
  def methods_of(path)

    tree = RubyTree.new(File.binread(path.to_s))
    out = []

    tree.each_node { |n, _|
      out << def_name(n) if DEFS.include?(n.type)
    }

    return out
  end

  # Drop the allowlisted violations. An entry is "file.rb" (the whole
  # file) or "file.rb#method"; a violation matches on either.
  def apply_allowlist(violations, allow)
    keys = allow.keys
    return violations.reject { |v|
      keys.include?(v.file) || keys.include?(v.where)
    }
  end
end
