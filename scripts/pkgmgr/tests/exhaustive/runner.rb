# SPDX-License-Identifier: BSD-2-Clause
#
# THE RUNNER: one case, many cases, and the check that the comparison
# works before it is believed.
#
# One case is: reset, build the shape's registry, build the world IN
# MEMORY (a World of the installs the candidates stand for, with no
# tree behind them), parse the argv with main's own parser into a
# Request, hand it to the planner (Planner.step) and to the model
# (Model.step), and compare the world each says the command line
# leaves, and the exit code. Nothing is written and nothing is
# scanned: what a command line does is a value, and the two values
# are compared. What the executor makes of a plan is judged once per
# kind of action, on disk, in test_executor.rb; and the laws around
# every command line the suite drives compare the planner's answer
# with the tree as well (tests/laws.rb, L1_executor).
#
# The self-test comes first, always. A comparison that cannot find a
# subject equal to itself has no business reporting differences: the
# runner checks that a world built in memory equals the scan of the
# same world built on disk, that the planner and the model each give
# one answer twice, that an empty plan applied is the identity, and
# that a planted disagreement is seen -- and refuses to run the lane
# if any of those fail.
#
# The full lane forks one process per shape: the cases of a shape are
# independent and a process keeps its own package manager singleton,
# which is exactly the isolation the cases need.
#

require 'stringio'
require 'tmpdir'
require 'etc'
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

  # --- the lane's surroundings ---------------------------------------------

  # What every case runs inside: a fake toolchain, so that no path
  # names the real one and the arches have their fake compiler
  # version; the externals stubbed; and the manager's own reading of
  # the tree refused, since the world is an argument here and a
  # planner that read the tree behind the argument's back would read
  # an empty one and be wrong in silence.
  def in_lane
    on_disk do
      pm = pkgmgr
      pm.define_singleton_method(:world) {
        raise "the lane's world is an argument: nothing reads the tree"
      }
      begin
        yield
      ensure
        pm.singleton_class.send(:remove_method, :world)
      end
    end
  end

  # The same surroundings with the tree readable: for the one check
  # that builds a world on disk to hold the value to.
  def on_disk
    h = harness
    h.with_fake_tc do
      h.with_stubbed_externals do
        yield
      end
    end
  end

  # The command line as main reads it, once per distinct line.
  def request_of(argv)
    @requests ||= {}
    @requests[argv] ||= Main.request_of(Main.parse_options(argv.dup))
  end

  # --- one case -------------------------------------------------------------

  # The case's registry, fresh: the registry is reset between cases and
  # a package must carry nothing over.
  def register(shape)
    pkgs = SHAPES.fetch(shape).call
    pkgs.each { |p| pkgmgr.register(p) }
    pkgmgr.default_stack = STACK_A
    return pkgs.to_h { |p| [p.name, p] }
  end

  def run_case(c)

    harness.reset_pkgmgr!
    by_name = register(c.shape)
    req = request_of(c.argv)

    # -H, as main takes it: the stack the run is in, once the compiler
    # package says it can build it.
    stack = req.stack || STACK_A
    scope = scope_for(c.ctx, stack: stack)
    world = world_of(c.world, by_name, scope)

    got = if req.stack && !pkgmgr.stack_compiler&.installable_versions
                                  &.include?(req.stack)
      Outcome.refused(world, "Unknown host GCC stack: #{req.stack}")
    else
      Planner.step(pkgmgr, world, req, scope)
    end
    want = Model.step(Bridge.registry(scope), keys_of(c.world),
                      Model.parse(c.argv), Bridge.inv(scope))

    problems = compare(c.argv, keys_of(c.world), got, want)
    detail = problems.empty? ? "" : "#{c}\n#{problems.join("\n\n")}"
    return Result.new(c.id, problems.empty?, detail)
  rescue StandardError => e
    return Result.new(c.id, false, "#{c}\nraised #{e.class}: #{e.message}\n" +
                                   e.backtrace.first(8).join("\n"))
  end

  # The planner's outcome against the model's: the world each leaves,
  # and the exit code. Words, empty when they agree.
  def compare(argv, before, got, want)
    out = []
    left = Bridge.keys_of_world(got.world)
    if left != want.world
      out << "L1_planner  #{argv.join(' ')}\n" +
             Laws.worlds(before, left, want.world, subject: "planner")
    end
    if got.rc != want.rc
      out << "rc  #{argv.join(' ')}\n  planner: #{got.rc}\n" \
             "  model:   #{want.rc}"
    end
    return out
  end

  # The candidates of a world as the model's keys.
  def keys_of(cands)
    return cands.map { |x|
      Model::Key.new(name: x.name, ver: x.ver, coords: x.coords,
                     record: x.record, origin: x.origin, mark: x.mark)
    }.to_set
  end

  # --- the self-test --------------------------------------------------------

  # Each check is a comparison that must come out EQUAL. Returns the
  # problems found, empty when the instrument can be trusted.
  def self_test
    return world_self_test + in_lane { planner_self_test }
  end

  # A world built in memory is the world built on disk: for one
  # two-install world per shape, fake_install each candidate, scan
  # the tree, and hold the scan to the value -- every field of every
  # install, path included, and the record once judged.
  def world_self_test

    problems = []
    h = harness

    for shape in SHAPES.keys do
      c = each_case([shape]).find { |x| x.world.length == 2 }
      next if c.nil?
      h.reset_pkgmgr!
      by_name = register(shape)

      # A tree of its own per shape, under the case's context.
      on_disk do
        h.with_context(ARCH: c.ctx.arch, BOARD: c.ctx.board) do
          scope = scope_for(c.ctx)
          built = world_of(c.world, by_name, scope)
          c.world.each { |cand|
            h.fake_install(by_name.fetch(cand.name), cand.ver,
                           at: cand.coords, record: cand.record,
                           origin: cand.origin, mark: cand.mark)
          }
          read = World.scan(pkgmgr.all_packages)
          if read.installs.to_set != built.installs.to_set
            problems << "#{shape}: the world built in memory is not the " \
                        "world on disk:\n  memory: " \
                        "#{built.installs.map(&:to_s)}\n  disk:   " \
                        "#{read.installs.map(&:to_s)}"
          end
          judged = read.judged(pkgmgr, scope)
          a = Bridge.keys_of_world(built).map(&:to_s).sort
          b = Bridge.keys_of_world(judged).map(&:to_s).sort
          if a != b
            problems << "#{shape}: the records differ once judged:\n" \
                        "  memory: #{a}\n  disk:   #{b}"
          end
        end
      end
    end

    return problems
  end

  # The planner and the model each give one answer twice, an empty
  # plan applied is the identity, and a planted disagreement is seen.
  def planner_self_test

    problems = []
    h = harness
    c = each_case(["target_2v"]).find { |x| x.world.length == 2 }
    h.reset_pkgmgr!
    by_name = register(c.shape)
    scope = scope_for(c.ctx)
    world = world_of(c.world, by_name, scope)
    req = request_of(c.argv)
    a = Bridge.keys_of_world(Planner.step(pkgmgr, world, req, scope).world)
    b = Bridge.keys_of_world(Planner.step(pkgmgr, world, req, scope).world)
    problems << "the planner is not deterministic" if a != b

    reg = Bridge.registry(scope)
    ask = -> { Model.step(reg, keys_of(c.world), Model.parse(c.argv),
                          Bridge.inv(scope)) }
    problems << "the model is not deterministic" if ask.call != ask.call

    same = Plan.new(actions: [], scope: scope, bound: {}, notes: [])
               .apply(pkgmgr, world)
    if same.installs.to_set != world.installs.to_set
      problems << "an empty plan applied is not the identity"
    end

    # ...and a planted disagreement is seen.
    wrong = Outcome.ok(World.of([]))
    if compare(c.argv, keys_of(c.world), wrong,
               Model::Outcome.new(0, keys_of(c.world), "")).empty?
      problems << "a planted disagreement went unreported"
    end

    return problems
  end

  # --- many cases -----------------------------------------------------------

  Summary = Struct.new(:shape, :total, :failed, :seconds)

  # `progress`, when given, is told (done, failed, seconds) every
  # PROGRESS_CASES cases: a shape of sixty thousand runs for minutes,
  # and a lane that says nothing for minutes looks hung.
  PROGRESS_CASES = 500

  def run_shape(shape, limit: nil, progress: nil)
    failed = []
    total = 0
    t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    now = -> { Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0 }

    each_case([shape]).each { |c|
      break if limit && total >= limit
      total += 1
      r = run_case(c)
      failed << r if !r.ok
      if progress && total % PROGRESS_CASES == 0
        progress.call(total, failed.length, now.call)
      end
    }

    return [Summary.new(shape, total, failed.length, now.call), failed]
  end

  PROGRESS_SECONDS = 30

  # The full lane: every shape in its own process, results through a
  # file each, a summary and every failure printed by the parent --
  # and, while they run, a progress line per shape every half minute.
  def run_all(shapes: SHAPES.keys, limit: nil, jobs: nil)

    problems = self_test
    if !problems.empty?
      puts "exhaustive: the instrument failed its self-test:"
      problems.each { |p| puts "  #{p}" }
      return false
    end

    jobs ||= [Etc.nprocessors, shapes.length].min
    dir = Dir.mktmpdir("pkgmgr-exhaustive-")
    $stdout.sync = true       # progress reaches a log as it happens
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

    # Every shape still running, as far as it has got: read from the
    # file its process keeps current, printed every PROGRESS_SECONDS.
    print_progress = -> {
      running.values.sort.each { |shape|
        f = File.join(dir, "#{shape}.progress")
        next if !File.exist?(f)
        done, bad, sec = File.read(f).split.map(&:to_f)
        printf("  %-14s %7d/%-7d      %4d failed  %6.1fs ...\n",
               shape, done, count(shape), bad, sec)
      }
    }

    last = Process.clock_gettime(Process::CLOCK_MONOTONIC)

    while !queue.empty? || !running.empty?
      while running.length < jobs && !queue.empty?
        shape = queue.shift
        pid = Process.fork {
          $stdout.reopen(File::NULL)
          f = File.join(dir, "#{shape}.progress")
          out = in_lane {
            run_shape(shape, limit: limit, progress: ->(*a) {
              File.write(f, a.join(" "))
            })
          }
          File.binwrite(File.join(dir, shape), Marshal.dump(out))
          exit!(0)
        }
        running[pid] = shape
      end

      pid = Process.wait(-1, Process::WNOHANG)
      if pid
        print_summary.call(running.delete(pid))
        next
      end

      sleep 1
      if Process.clock_gettime(Process::CLOCK_MONOTONIC) - last >=
         PROGRESS_SECONDS
        print_progress.call
        last = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      end
    end

    FileUtils.rm_rf(dir)
    return all_ok
  end
end
