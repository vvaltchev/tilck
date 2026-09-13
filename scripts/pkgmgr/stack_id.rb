# SPDX-License-Identifier: BSD-2-Clause
#
# THE STACK COORDINATE, AS A VALUE.
#
# The third coordinate of an installation (coords.rb) names the build
# environment that produced it. Its grammar is
#
#   <family>-<version>[-<variant>]      gcc-14.4.0, gcc-14.4.0-lto,
#                                       clang-18.1.0
#
# and this is the one place that grammar is known. The schema has
# promised since toolchain5 that a variant stack is legal without a
# schema change; the code, meanwhile, read every stack directory with
# a sub("gcc-", "") of its own and took anything else for an orphan.
# Now a directory is a stack when this parses it, a stack is spelled
# by to_s, and the compiler version inside it is `ver`, whatever the
# family and the variant are.
#
# What can be BUILT INTO is narrower than what can be read: the
# invocation's stack (Scope#stack) is still the version of the one
# family there is, gcc, without a variant. A variant or foreign stack
# on disk is scanned, listed and identified by its coordinates; being
# able to select one with -H is the step after this one.
#
require_relative 'version'

StackId = Data.define(:family, :ver, :variant) do

  GRAMMAR = /\A([a-z][a-z0-9_]*)-(\d+(?:\.\d+)*)(?:-([a-z0-9][a-z0-9_.-]*))?\z/

  # The one family the tool builds into today.
  GCC = "gcc"

  # A stack from its spelling in a path, or nil when the string is not
  # one: ANY, a bare version, a package name, a word. The version
  # group is digits and dots, which Version always reads.
  def self.parse(str)
    m = GRAMMAR.match(str.to_s)
    return nil if m.nil?
    return new(family: m[1], ver: Ver(m[2]), variant: m[3])
  end

  # The plain gcc stack of a compiler version: what a Version has
  # always meant where a stack was expected.
  def self.of(ver, family: GCC, variant: nil)
    return new(family: family, ver: ver, variant: variant)
  end

  # A StackId as given, or the plain stack of a Version.
  def self.coerce(x)
    return x if x.is_a?(StackId)
    return of(x) if x.is_a?(Version)
    raise ArgumentError, "not a stack: #{x.inspect}"
  end

  # The one kind the tool can build into: gcc, no variant.
  def plain? = family == GCC && variant.nil?

  def to_s = [family, ver.to_s, variant].compact.join("-")

  include Comparable
  def <=>(other)
    return nil if !other.is_a?(StackId)
    return [family, ver, variant.to_s] <=> [other.family, other.ver,
                                            other.variant.to_s]
  end
end
