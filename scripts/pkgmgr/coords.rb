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
#             gcc-14.4.0-lto legal without changing the schema
#             any, gcc-14.4.0, gcc-13.3.0
#
# ANY is a reserved word in all three: no distro, board or stack may
# be called it.
#
require_relative 'version'

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
    freeze
  end

  def check(what, value)
    s = value.to_s
    raise "Coords: blank #{what}" if s.strip.empty?
    return s
  end
  private :check

  # The version in a stack NAME, written either way: "gcc-14.4.0", as
  # it appears in a path and in the -L listing, or the bare "14.4.0"
  # that names it just as unambiguously while there is one kind of
  # stack. nil when it is neither.
  #
  # Here rather than in the option parser because the spelling of a
  # stack is the schema's business, and stack_ver right below is the
  # same knowledge read in the other direction.
  def self.parse_stack(str)
    return SafeVer(str.to_s.strip.sub(/\Agcc-/, ""))
  end

  # The name a stack version is filed under. The inverse of
  # parse_stack, so callers that print a stack agree with callers that
  # read one.
  def self.stack_name(ver) = "gcc-#{ver}"

  # The compiler version this stack names, or nil when the stack is
  # not a GCC one -- ANY, or the clang-* the schema leaves room for.
  #
  # Lives here because the spelling of a stack is the schema's
  # business: everyone who needed the version was re-deriving it with
  # a sub("gcc-", "") of their own.
  def stack_ver
    return nil if @stack == ANY || !@stack.start_with?("gcc-")
    return SafeVer(@stack.sub("gcc-", ""))
  end

  # The three coordinates as a path fragment, for messages.
  def to_s = "#{@machine}/#{@env}/#{@stack}"

  def ==(other)
    return false if !other.is_a?(Coords)
    return to_s == other.to_s
  end

  def eql?(other) = self == other
  def hash = to_s.hash

  # Resolved against TC at call time, never at load time: the tests
  # override TC to a temporary tree.
  def root = TC / @machine / @env / @stack

  # Installations. Packages are one level below their stack so that a
  # sysroot -- a view, not an installation -- can sit beside them
  # without any scanner having to be taught to skip it.
  def pkgs_dir = root / "pkgs"

  # The composed sysroot of this stack, when we built the environment.
  def sysroot = root / "sysroot"

  # Does this stack have a sysroot? Only when the environment is ours:
  # a package that needs the distro is compiled against the distro's
  # own headers and libraries, and there is nothing for us to compose.
  def own_env? = @env == ANY && @stack != ANY
end
