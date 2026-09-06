# SPDX-License-Identifier: BSD-2-Clause
#
# THE RUNNER: one case, many cases, and the check that the comparison
# works before it is believed.
#
# One case is: reset, build the shape's registry, build the world with
# fake_install (record and origin included), open the context, take a
# snapshot, run Main.main on the argv, take another, and hand the two
# to the laws -- L1 is the model's verdict, L2..L4 the others. A case
# passes when the laws are silent.
#
# The self-test comes first, always. A comparison that cannot find a
# subject equal to itself has no business reporting differences: the
# runner checks that a snapshot equals a second snapshot of the same
# tree, that the model gives one answer twice, and that a world built
# by fake_install reads back exactly as it was built -- and refuses to
# run the lane if any of those fail.
#
# The full lane forks one process per shape: the cases of a shape are
# independent and a process keeps its own package manager singleton,
# which is exactly the isolation the cases need.
#

require 'stringio'
require 'tmpdir'
require_relative 'domain'
require_relative '../laws'
require_relative '../model/bridge'
require_relative '../../main'

module Exhaustive

  Result = Struct.new(:id, :ok, :detail) do
    def to_s = ok ? "ok #{id}" : "FAIL #{id}\n#{detail}"
  end

  # The harness methods live in TestHelper as instance methods; one
  # object carries them here.
  class Harness
    include TestHelper
  end

  module_function

  def harness = (@harness ||= Harness.new)

  # --- one case -------------------------------------------------------------

  def run_case(c)

    h = harness
    h.reset_pkgmgr!
    TestHelper::FakePackage.clear_log!

    h.with_fake_tc do
      h.with_context(ARCH: c.ctx.arch, BOARD: c.ctx.board) do
        h.with_stubbed_externals do
          pkgs = SHAPES.fetch(c.shape).call
          pkgs.each { |p| pkgmgr.register(p) }
          by_name = pkgs.to_h { |p| [p.name, p] }

          # The stack in effect: what -H would otherwise set.
          pkgmgr.host_stack = STACK_A

          for cand in c.world do
            h.fake_install(by_name.fetch(cand.name), cand.ver,
                           at: cand.coords, record: cand.record,
                           origin: cand.origin)
          end

          before = Bridge.snapshot
          run_main(c.argv)
          after = Bridge.snapshot

          broken = Laws.check(c.argv, before, after)
          detail = broken.map(&:to_s).join("\n\n")
          detail = "#{c}\n#{detail}" if !broken.empty?
          return Result.new(c.id, broken.empty?, detail)
        end
      end
    end
  end

  def run_main(argv)
    old = $stdout
    $stdout = StringIO.new
    begin
      Main.main(argv.dup)
    rescue SystemExit
      nil
    ensure
      $stdout = old
    end
  end

  # --- the self-test --------------------------------------------------------

  # Each check is a comparison that must come out EQUAL. Returns the
  # problems found, empty when the instrument can be trusted.
  def self_test

    problems = []
    h = harness

    # A world built by fake_install reads back as it was built, twice.
    c = each_case(["target_2v"]).find { |x| x.world.length == 2 }
    h.reset_pkgmgr!

    h.with_fake_tc do
      h.with_context(ARCH: c.ctx.arch, BOARD: c.ctx.board) do
        h.with_stubbed_externals do
          pkgs = SHAPES.fetch(c.shape).call
          pkgs.each { |p| pkgmgr.register(p) }
          by_name = pkgs.to_h { |p| [p.name, p] }
          pkgmgr.host_stack = STACK_A

          c.world.each { |cand|
            h.fake_install(by_name.fetch(cand.name), cand.ver,
                           at: cand.coords, record: cand.record,
                           origin: cand.origin)
          }

          one = Bridge.snapshot
          two = Bridge.snapshot
          want = c.world.map { |x|
            Model::Key.new(name: x.name, ver: x.ver, coords: x.coords,
                           record: x.record, origin: x.origin)
          }.to_set

          if one.world != two.world
            problems << "two snapshots of one tree differ"
          end

          if one.world != want
            problems << "the world read back is not the world built:\n" \
                        "  built: #{want.map(&:to_s).sort}\n" \
                        "  read:  #{one.world.map(&:to_s).sort}"
          end

          # The model gives one answer twice.
          req = Model.parse(c.argv)
          a = Model.step(one.registry, one.world, req, one.inv)
          b = Model.step(one.registry, one.world, req, one.inv)
          problems << "the model is not deterministic" if a != b

          # ...and a planted disagreement is seen.
          wrong = Bridge::Snapshot.new(registry: one.registry,
                                       world: Set.new, inv: one.inv,
                                       misplaced: [])
          if Laws.check(["-l", "-q"], one, wrong).empty?
            problems << "a planted disagreement went unreported"
          end
        end
      end
    end

    return problems
  end

  # --- many cases -----------------------------------------------------------

  Summary = Struct.new(:shape, :total, :failed, :seconds)

  def run_shape(shape, limit: nil)
    failed = []
    total = 0
    t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)

    each_case([shape]).each { |c|
      break if limit && total >= limit
      total += 1
      r = run_case(c)
      failed << r if !r.ok
    }

    dt = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
    return [Summary.new(shape, total, failed.length, dt), failed]
  end

  # The full lane: every shape in its own process, results through a
  # file each, a summary and every failure printed by the parent.
  def run_all(shapes: SHAPES.keys, limit: nil, jobs: nil)

    problems = self_test
    if !problems.empty?
      puts "exhaustive: the instrument failed its self-test:"
      problems.each { |p| puts "  #{p}" }
      return false
    end

    jobs ||= [Etc.nprocessors, shapes.length].min
    dir = Dir.mktmpdir("pkgmgr-exhaustive-")
    queue = shapes.dup
    running = {}
    all_ok = true

    print_summary = ->(shape) {
      s, failed = Marshal.load(File.binread(File.join(dir, shape)))
      all_ok = false if s.failed > 0
      printf("  %-14s %7d cases  %4d failed  %6.1fs\n",
             s.shape, s.total, s.failed, s.seconds)
      failed.each { |r| puts; puts r.to_s }
    }

    while !queue.empty? || !running.empty?
      while running.length < jobs && !queue.empty?
        shape = queue.shift
        pid = Process.fork {
          $stdout.reopen(File::NULL)
          out = run_shape(shape, limit: limit)
          File.binwrite(File.join(dir, shape), Marshal.dump(out))
          exit!(0)
        }
        running[pid] = shape
      end

      pid = Process.wait
      print_summary.call(running.delete(pid))
    end

    FileUtils.rm_rf(dir)
    return all_ok
  end
end
