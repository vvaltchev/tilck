# SPDX-License-Identifier: BSD-2-Clause

#
# Where an installed package lives, as three coordinates.
#
#   <machine>/<env>/<stack>/{ sysroot/, pkgs/<pkg>/<ver>/ }
#
# Always three, always in that order, each with a fixed meaning. That
# is the whole point: a package name can appear only under pkgs/, so
# it can never be mistaken for structure. toolchain4 had a level that
# held either a package or a compiler depending on which, and needed a
# predicate to guess -- see docs/plans/toolchain5.md.
#
#   machine   where the artifact RUNS
#             linux-x86_64, tilck-i386, tilck-riscv64, noarch
#
#   env       which environment it belongs to: what the machine must
#             already provide, or -- for a Tilck target -- which board
#             it was built for
#             any, ubuntu-22.04, pc, qemu-virt
#
#   stack     which build environment produced it. Deliberately NOT
#             "the compiler": its values look like compilers today,
#             but naming it this way leaves gcc-13.3.0-musl and
#             gcc-14.4.0-lto legal without changing the schema. The
#             grammar, <family>-<version>[-<variant>], is StackId's
#             (stack_id.rb): a directory is a stack when it parses.
#             any, gcc-14.4.0, gcc-13.3.0
#
# ANY is a reserved word in all three: no distro, board or stack may
# be called it.
#
require_relative 'version'
require_relative 'stack_id'

class Coords

  ANY = "any"

  attr_reader :machine, :env, :stack

  # nil means "not applicable here" and becomes ANY: a noarch package
  # has no environment and no stack, and says so.
  #
  # An empty string is different, and is refused. It means a caller
  # computed a coordinate and got nothing -- which used to collapse the
  # path to two levels, `tilck-i386/gcc-13.3.0`, in a schema whose
  # whole promise is that there are always exactly three. That is the
  # toolchain4 ambiguity this class exists to remove, so it must not be
  # reachable by accident.
  def initialize(machine, env, stack)
    @machine = check("machine", machine)
    @env = env.nil? ? ANY : check("env", env)
    @stack = stack.nil? ? ANY : check("stack", stack)
    @key = "#{@machine}/#{@env}/#{@stack}"
    freeze
  end

  def check(what, value)
    s = value.to_s
    raise "Coords: blank #{what}" if s.strip.empty?
    return s
  end
  private :check

  # A stack as a PERSON writes it: "gcc-14.4.0", as it appears in a
  # path and in the -L listing, or the bare "14.4.0" that names the
  # plain gcc stack just as unambiguously. A StackId, or nil when it
  # is neither.
  #
  # Here rather than in the option parser because the spelling of a
  # stack is the schema's business, and stack_id right below is the
  # same knowledge read in the other direction.
  def self.parse_stack(str)
    s = str.to_s.strip
    bare = SafeVer(s)
    return StackId.of(bare) if bare
    return StackId.parse(s)
  end

  # The compiler version of the plain gcc stack a person named -- the
  # one kind of stack an invocation can be in (Scope#stack) -- or nil
  # when the string names no stack, or a stack of another kind.
  def self.parse_stack_ver(str)
    id = parse_stack(str)
    return id&.plain? ? id.ver : nil
  end

  # The name a stack is filed under: a StackId's spelling, or the
  # plain gcc stack of a compiler version. The inverse of parse_stack,
  # so callers that print a stack agree with callers that read one.
  def self.stack_name(stack) = StackId.coerce(stack).to_s

  # The stack these coordinates name, or nil for a directory that is
  # not spelled like a stack -- ANY among them -- which the schema's
  # scanners therefore leave alone.
  def stack_id = StackId.parse(@stack)

  # The compiler version inside the stack, whatever its family and
  # variant; nil where there is no stack.
  def stack_ver = stack_id&.ver

  # The three coordinates as a path fragment, for messages.
  def to_s = @key

  def ==(other)
    return false if !other.is_a?(Coords)
    return to_s == other.to_s
  end

  def eql?(other) = self == other
  def hash = @key.hash

  # Paths are resolved against TC at call time, never at load time --
  # the tests override TC to a temporary tree -- and remembered per
  # tree: a scan asks the same coordinates for the same path thousands
  # of times, and Pathname arithmetic was a third of an exhaustive
  # case. A different TC is a different tree and empties the memo,
  # which is what keeps the tests' override honest.
  @@paths = {}
  # mutation: equivalent -- anything that is not a tree means no tree yet
  @@paths_tc = nil

  def self.remembered(key)
    if !@@paths_tc.equal?(TC)
      @@paths_tc = TC
      @@paths.clear
    end
    return @@paths[key] ||= yield
  end

  def root = Coords.remembered([:root, @key]) { TC / @machine / @env / @stack }

  # Installations. Packages are one level below their stack so that a
  # sysroot -- a view, not an installation -- can sit beside them
  # without any scanner having to be taught to skip it.
  def pkgs_dir = Coords.remembered([:pkgs, @key]) { root / "pkgs" }

  # One package's directory under pkgs/, by its directory name.
  def pkg_dir(dirname)
    return Coords.remembered([:pkg, @key, dirname]) { pkgs_dir / dirname }
  end

  # The composed sysroot of this stack, when we built the environment.
  def sysroot = Coords.remembered([:sysroot, @key]) { root / "sysroot" }

  # <machine>/<env>: the directory the stacks are listed from.
  def self.env_dir(machine, env)
    e = env || ANY
    return remembered([:env, machine, e]) { TC / machine / e }
  end
end
