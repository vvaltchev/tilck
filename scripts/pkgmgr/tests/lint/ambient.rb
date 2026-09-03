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
#   R3  a write to a scope variable (@target_arch, @target_board,
#       @portable_stack) outside the with_* method that owns it, so a
#       scope cannot be left open.
#
# Prism, not regexes: a string "ARCH=x86" in a make invocation is not
# a read, a comment is not a read, and a receiver is not a bare name.
#
# It is a test, so a new reader fails the suite the day it is written,
# with file, method and line. An allowlist entry needs a reason, and
# the test that the entry still names a real method is what stops the
# list from rotting.
#

require 'prism'
require 'pathname'

module AmbientLint

  AMBIENT      = %i[ARCH BOARD DEFAULT_BOARD HOST_VER_GCC].freeze
  COMPARISONS  = %i[== != eql?].freeze

  # `arch` and `compiler` are each a part of a coordinate, and an
  # InstallInfo still carries them. `target_arch` is not one: it is
  # what a cross compiler produces code FOR, metadata a compiler
  # install has and nothing else does, so selecting a compiler by it
  # is the right question rather than a partial one.
  PARTIAL_KEYS = %i[arch compiler].freeze

  # The scope lives in PackageManager. Other classes have an ivar of
  # the same name meaning something else -- GccPackage's @target_arch
  # is the arch it targets -- and those are not scopes.
  SCOPE_IVARS  = %i[@target_arch @target_board @portable_stack].freeze
  SCOPE_FILE   = "package_manager.rb"

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
    result = Prism.parse(src)
    out = []
    walk(result.value, file, nil, src, out)
    return out
  end

  # Depth-first, remembering the enclosing method. A module_function
  # module (Main, Layout) and a class body look the same to this: a
  # DefNode is a method, `def self.x` is "self.x".
  def walk(node, file, meth, src, out)

    return if node.nil?

    if node.is_a?(Prism::DefNode)
      meth = node.receiver.nil? ? node.name.to_s : "self.#{node.name}"
    end

    check(node, file, meth, src, out)
    node.compact_child_nodes.each { |c| walk(c, file, meth, src, out) }
  end

  def check(node, file, meth, src, out)

    line = node.location.start_line
    text = src.lines[line - 1].to_s.strip

    case node
    when Prism::ConstantReadNode
      if AMBIENT.include?(node.name)
        out << Violation.new(:R1, file, meth, line, text)
      end

    when Prism::CallNode
      if COMPARISONS.include?(node.name) &&
         node.receiver.is_a?(Prism::CallNode) &&
         PARTIAL_KEYS.include?(node.receiver.name)
        out << Violation.new(:R2, file, meth, line, text)
      end

    when Prism::InstanceVariableWriteNode,
         Prism::InstanceVariableOrWriteNode
      if file == SCOPE_FILE && SCOPE_IVARS.include?(node.name)
        out << Violation.new(:R3, file, meth, line, text)
      end
    end
  end

  # The methods a file defines, as "name" / "self.name" -- what an
  # allowlist entry has to still point at.
  def methods_of(path)

    src = File.binread(path.to_s)
    out = []

    each_def(Prism.parse(src).value) { |d|
      out << (d.receiver.nil? ? d.name.to_s : "self.#{d.name}")
    }

    return out
  end

  def each_def(node, &blk)
    return if node.nil?
    blk.call(node) if node.is_a?(Prism::DefNode)
    node.compact_child_nodes.each { |c| each_def(c, &blk) }
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
