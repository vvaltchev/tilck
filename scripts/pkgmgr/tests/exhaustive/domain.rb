# SPDX-License-Identifier: BSD-2-Clause
#
# THE DOMAIN: every small world, every context, every command line.
#
# Not a sample. For each registry shape in the catalogue, the lane
# enumerates every world of at most two installations that shape can
# have, every invocation context, and every command line in the
# grammar, and asks the implementation and the model the same
# question. The claim it establishes is bounded and exact: for these
# shapes and these worlds and these lines, the two agree.
#
# Two installations is not an arbitrary bound. Every logic bug this
# package manager has had manifested with exactly two of something --
# two boards, two versions, two stacks, two arches -- and a fixture
# with one of each cannot tell "the right one" from "the only one".
# When a bug ever appears at three, the bound goes to three.
#
# Shapes are small on purpose: one feature each, so a failure names
# the feature. A shape for a new package feature is added when the
# feature is.
#

require 'set'
require_relative '../test_helper'
require_relative '../model/model'

module Exhaustive

  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]

  STACK_A = Ver("7.7.7")
  STACK_B = Ver("8.8.8")

  # A fake with a version list: the first is the default.
  class Pkg < TestHelper::FakePackage

    def initialize(name, versions: ["1.0.0"], **kw)
      super(name, **kw)
      @versions = versions.map { |v| Ver(v) }
    end

    def default_ver = @versions.first
    def installable_versions = @versions
  end

  # The stack compiler, as the shapes need it: its default version is
  # the stack in effect and it can be asked for A or B, which is what
  # -H checks against and what a :stack package's dependency resolves
  # through. Mirrors HostGccPackage in the two things that matter.
  class FakeHostGcc < TestHelper::FakePackage

    def initialize
      super("host_gcc", on_host: true, host_tier: :distro,
            arch_list: ALL_HOST_ARCHS.values)
    end

    def default_ver = pkgmgr.current_host_stack
    def installable_versions = [STACK_A, STACK_B]
    def stack_gcc_ver(ver = nil) = ver || pkgmgr.current_host_stack
  end

  module_function

  def host(name, tier, **kw)
    Pkg.new(name, on_host: true, host_tier: tier,
            arch_list: ALL_HOST_ARCHS.values, **kw)
  end

  def stack_pkg(name, **kw)
    deps = kw.delete(:dep_list) || []
    host(name, :stack, dep_list: deps + [Dep("host_gcc", true)], **kw)
  end

  # --- the catalogue --------------------------------------------------------

  # name => a block returning fresh packages. Fresh per case: the
  # registry is reset between cases and a package must carry nothing
  # over.
  SHAPES = {
    "target"       => -> { [Pkg.new("t")] },
    "target_2v"    => -> { [Pkg.new("t", versions: %w[2.0.0 1.0.0])] },
    "target_rv"    => -> { [Pkg.new("rv", arch_list: [RV])] },
    "target_board" => -> { [Pkg.new("ub", arch_list: [RV],
                                    board_list: ["qemu-virt"])] },
    "noarch"       => -> { [Pkg.new("n", arch_list: nil)] },
    "portable"     => -> { [host("host_p", :portable)] },
    "distro"       => -> { [host("host_d", :distro)] },
    "compiler"     => -> { [host("host_c", :compiler)] },
    "stack"        => -> { [stack_pkg("host_s"), FakeHostGcc.new] },
    "stack_pin"    => -> { [stack_pkg("host_s",
                                      dep_list: [Dep("host_x", true,
                                                     ver: Ver("2.0.0"))]),
                            stack_pkg("host_x", versions: %w[1.0.0 2.0.0]),
                            FakeHostGcc.new] },
    "cross_cc"     => -> { [Pkg.new("t"),
                            Pkg.new("gcc-i386-musl", on_host: true,
                                    is_compiler: true, host_tier: :portable,
                                    arch_list: ALL_HOST_ARCHS.values,
                                    target_arch: I386)] },
    "chain"        => -> { [Pkg.new("a", dep_list: [Dep("b", false)]),
                            Pkg.new("b", dep_list: [Dep("c", false)]),
                            Pkg.new("c")] },
    "diamond"      => -> { [Pkg.new("a", dep_list: [Dep("b", false),
                                                    Dep("c", false)]),
                            Pkg.new("b", dep_list: [Dep("d", false)]),
                            Pkg.new("c", dep_list: [Dep("d", false)]),
                            Pkg.new("d")] },
    "conflict"     => -> { [host("host_a", :distro,
                                 dep_list: [Dep("host_x", true,
                                                ver: Ver("1.0.0"))]),
                            host("host_b", :distro,
                                 dep_list: [Dep("host_x", true,
                                                ver: Ver("2.0.0"))]),
                            host("host_x", :distro,
                                 versions: %w[1.0.0 2.0.0])] },
    "default"      => -> { [Pkg.new("dflt", default: true), Pkg.new("t")] },
  }.freeze

  # --- where an installation of a package could be ---------------------------

  def tgt(arch, board)
    Coords.new("tilck-#{arch.name}", board, "gcc-#{TestHelper::FAKE_GCC_VER}")
  end

  def stack(v) = Coords.new(HOST_OS_ARCH, nil, Coords.stack_name(v))

  def coords_for(pkg)
    if !pkg.on_host && pkg.arch_list.nil?
      [pkg.coords]
    elsif !pkg.on_host
      out = []
      out << tgt(I386, "pc") if pkg.arch_list.include?(I386)
      if pkg.arch_list.include?(RV)
        out << tgt(RV, "qemu-virt") << tgt(RV, "licheerv-nano")
      end
      out
    elsif pkg.host_tier == :stack
      [stack(STACK_A), stack(STACK_B)]
    else
      [pkg.coords]
    end
  end

  # One installation that could exist: what fake_install needs.
  Candidate = Struct.new(:name, :ver, :coords, :record, :origin) do
    def same_place?(o) = name == o.name && ver == o.ver && coords == o.coords
    def to_s = "#{name}@#{ver} #{coords} #{record}/#{origin}"
  end

  def candidates(pkgs)
    out = []
    for p in pkgs do
      for v in p.installable_versions.empty? ? [p.default_ver]
                                              : p.installable_versions do
        origins = v == p.default_ver ? [:default] : [:default, :pinned]
        for c in coords_for(p) do
          for r in [:ok, :changed] do
            for o in origins do
              out << Candidate.new(p.name, v, c, r, o)
            end
          end
        end
      end
    end
    return out
  end

  # Every world of at most `max` installations: the empty one, each
  # candidate alone, each pair that is not two records of one place.
  def worlds(cands, max: 2)
    out = [[]]
    out += cands.map { |c| [c] } if max >= 1
    if max >= 2
      cands.combination(2).each { |a, b|
        out << [a, b] if !a.same_place?(b)
      }
    end
    return out
  end

  # --- contexts -------------------------------------------------------------

  Ctx = Struct.new(:arch, :board) do
    def to_s = "#{arch.name}/#{board}"
  end

  CONTEXTS = [
    Ctx.new(I386, "pc"),
    Ctx.new(RV, "qemu-virt"),
    Ctx.new(RV, "licheerv-nano"),
  ].freeze

  # --- the grammar ----------------------------------------------------------

  QUERIES = ["--check-for-updates", "-l", "--list-installable",
             "--print-layout"].freeze

  def argv_lines(pkgs)

    lines = []

    for p in pkgs do
      n = p.name
      vers = p.installable_versions.empty? ? [p.default_ver]
                                           : p.installable_versions
      lines << "-s #{n}" << "-s #{n} -f" << "-u #{n}" << "-u #{n}:ALL" \
            << "-u #{n} -a riscv64" << "-u #{n} -a ALL" << "-C #{n}"
      vers.each { |v|
        lines << "-s #{n}:#{v}" << "-s #{n}:#{v} -f" << "-u #{n}:#{v}"
      }
      if !p.on_host && !p.arch_list.nil?
        lines << "-s #{n} -a riscv64" << "-s #{n} -a ALL" \
              << "-u #{n} -c #{TestHelper::FAKE_GCC_VER}"
      end
      if p.on_host && p.host_tier == :stack
        lines << "-s #{n} -H #{STACK_B}" << "-u #{n} -c #{STACK_B}" \
              << "-u #{n} -H #{STACK_B}"
      end
    end

    lines << "-s ALL" << "-u ALL" << "-u ALL -f" << "-u ALL -a ALL" \
          << "-u ALL -c #{TestHelper::FAKE_GCC_VER}" << "--upgrade" \
          << "--clean" << ""

    lines = lines.uniq
    lines += lines.map { |l| "#{l} -d".strip }
    lines += QUERIES

    return lines.map { |l| (l.split + ["-q"]) }
  end

  # --- cases ----------------------------------------------------------------

  Case = Struct.new(:id, :shape, :world, :ctx, :argv) do
    def to_s
      "#{id}\n  world: #{world.empty? ? "(empty)" : world.join(", ")}\n" \
      "  ctx:   #{ctx}\n  argv:  #{argv.join(' ')}"
    end
  end

  # The per-shape tables, built once per process.
  Tables = Struct.new(:worlds, :argvs)

  def tables_for(shape)
    @tables ||= {}
    @tables[shape] ||= begin
      pkgs = SHAPES.fetch(shape).call
      Tables.new(worlds(candidates(pkgs)), argv_lines(pkgs))
    end
  end

  def count(shape)
    t = tables_for(shape)
    return t.worlds.length * CONTEXTS.length * t.argvs.length
  end

  # ids are "shape/world/ctx/argv" with the three indexes, so that a
  # failure is replayable from its id alone.
  def each_case(shapes = SHAPES.keys)

    return to_enum(:each_case, shapes) if !block_given?

    for shape in shapes do
      t = tables_for(shape)
      t.worlds.each_with_index { |w, wi|
        CONTEXTS.each_with_index { |c, ci|
          t.argvs.each_with_index { |a, ai|
            yield Case.new("#{shape}/#{wi}/#{ci}/#{ai}", shape, w, c, a)
          }
        }
      }
    end
  end

  def case_by_id(id)
    shape, wi, ci, ai = id.split("/")
    t = tables_for(shape)
    return Case.new(id, shape, t.worlds.fetch(wi.to_i),
                    CONTEXTS.fetch(ci.to_i), t.argvs.fetch(ai.to_i))
  end

  # A fixed-seed sample of `n` ids across every shape, evenly.
  def sample_ids(n, seed:)
    rng = Random.new(seed)
    per = (n.to_f / SHAPES.length).ceil
    ids = []
    for shape in SHAPES.keys do
      t = tables_for(shape)
      per.times {
        ids << "#{shape}/#{rng.rand(t.worlds.length)}/" \
               "#{rng.rand(CONTEXTS.length)}/#{rng.rand(t.argvs.length)}"
      }
    end
    return ids.uniq.first(n)
  end
end
