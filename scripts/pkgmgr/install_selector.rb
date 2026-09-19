# SPDX-License-Identifier: BSD-2-Clause
#
# WHICH INSTALLATIONS AN OPERATION MEANS.
#
# An installation is identified by three things: a package name, a
# version, and its coordinates. Every operation that acts on one --
# uninstall, force-remove, "is it installed" -- has to say all three,
# and for years the code said them as a tuple of loose arguments
# (ver, compiler, arch, plus "ALL" strings in any of them) matched
# field by field against InstallInfo. Each field alone is a partial
# key. An arch is two thirds of a coordinate, so matching on it took
# both boards of riscv64; a nil compiler read as "must be nil", so a
# forced rebuild removed nothing; a stack package answered "syscc" and
# matched nothing at all.
#
# This is the whole key as one value. `where` is a union of
# coordinate filters, each level a string or :any; `matches?` is the
# only identity test, and it names no field of a coordinate on its
# own. The lint (tests/lint/ambient.rb, rule R2) keeps it that way.
#

require_relative 'coords'

# One level-by-level test on Coords. :any at a level matches every
# value there; a string matches itself. `exact(c)` is the filter that
# matches c and nothing else.
CoordsFilter = Data.define(:machine, :env, :stack) do

  ANY = :any

  def self.any = new(machine: ANY, env: ANY, stack: ANY)
  def self.exact(c) = new(machine: c.machine, env: c.env, stack: c.stack)

  def include?(c)
    return false if c.nil?
    return (machine == ANY || machine == c.machine) &&
           (env     == ANY || env     == c.env) &&
           (stack   == ANY || stack   == c.stack)
  end

  def to_s = "#{machine}/#{env}/#{stack}"
end

class InstallSelector

  attr_reader :name, :ver, :where

  # name:  String | :all
  # ver:   Version | :all
  # where: [CoordsFilter, ...] -- a union; empty means "nowhere"
  def initialize(name:, ver:, where:)
    @name = name
    @ver = ver
    @where = where.freeze
    freeze
  end

  def matches?(inst)
    return (name == :all || inst.pkgname == name) &&
           (ver  == :all || inst.ver == ver) &&
           where.any? { |f| f.include?(inst.coords) }
  end

  def to_s
    "#{name == :all ? "ALL" : name}:#{ver == :all ? "ALL" : ver} at " \
    "#{where.map(&:to_s).join(' | ')}"
  end
end
