# SPDX-License-Identifier: BSD-2-Clause
#
# THE LAWS: what every command line must obey, checked after every
# one the suite runs.
#
# Not assertions a test remembers to write -- checks the harness makes
# on its own, around every Main.main it drives (TestHelper#run_cli).
# A test about -l output is also, without saying so, a test that -l
# changed nothing; a test about -f rebuilding one package is also a
# test that it rebuilt exactly the packages the model says.
#
#   L1  MODEL      the world after equals what the model computes from
#                  the world before and the same argv. The whole
#                  contract, applied to whatever the test happened to
#                  set up. Skipped, and counted, when the argv is
#                  outside the model's grammar. Asked three ways, so
#                  that a disagreement names its layer: the tree
#                  against the model (L1_model), the planner's own
#                  answer -- Planner.step on the world before, its
#                  plans applied (Plan#apply) -- against the model
#                  (L1_planner), and the tree against the planner's
#                  answer (L1_executor): a plan the executor did not
#                  carry out as written.
#   L2  DRY-RUN    -d anywhere in argv: the world is unchanged. Checked
#                  even when L1 cannot parse the line.
#   L3  PLACEMENT  every installation on disk sits exactly where its
#                  package says an install of that version goes, judged
#                  at the installation's own coordinates. Measured by
#                  the bridge, since only the implementation can be
#                  asked; reported here.
#   L4  RECORDED   everything this run installed has a record and it
#                  reads :ok.
#
# A violation names the law, the argv, and the two worlds, so a
# disagreement is debuggable from the message alone.
#

require 'set'
require_relative 'model/model'

module Laws

  Violation = Struct.new(:law, :argv, :detail) do
    def to_s = "#{law}  #{argv.join(' ')}\n#{detail}"
  end

  # What the laws saw: how many command lines were judged against the
  # model, and which ones the model could not parse. The runner prints
  # both at the end, so a suite that silently stopped checking would
  # say so in its own summary.
  @unparsed = []
  @checked = 0

  class << self
    attr_reader :unparsed
    attr_accessor :checked
  end

  module_function

  def check(argv, before, after)

    out = []
    req = parse(argv)

    if req
      Laws.checked += 1
      expected = Model.step(before.registry, before.world, req, before.inv)

      if after.world != expected.world
        out << Violation.new(:L1_model, argv, worlds(before.world, after.world,
                                                   expected.world))
      end

      planned = planned(argv, before)
      if planned && planned != expected.world
        out << Violation.new(:L1_planner, argv,
                             worlds(before.world, planned, expected.world,
                                    subject: "planner"))
      end
      if planned && after.world != planned
        out << Violation.new(:L1_executor, argv,
                             worlds(before.world, after.world, planned,
                                    oracle: "planner"))
      end
    end

    if argv.include?("-d") && before.world != after.world
      out << Violation.new(:L2_dry_run, argv,
                           worlds(before.world, after.world, before.world))
    end

    for m in after.misplaced do
      out << Violation.new(:L3_placement, argv, m)
    end

    # L5: what the package manager holds about the tree is what the
    # tree says. Its install lists are re-read once per change, and a
    # change nobody announced is a list that lies until the next one.
    for m in Array(after.stale) do
      out << Violation.new(:L5_installs, argv, m)
    end

    if !argv.include?("-d")
      # A mark is a note on an install, not an install: a key that
      # differs from before only by its mark was not built.
      was = before.world.map { |k| k.with(mark: nil) }.to_set
      touched = after.world.reject { |k| was.include?(k.with(mark: nil)) }
      bad = touched.reject { |k| k.record == :ok }
      if !bad.empty?
        out << Violation.new(:L4_recorded, argv,
                             "installed without an :ok record:\n" +
                             bad.map { |k| "  #{k}" }.join("\n"))
      end
    end

    return out
  end

  def parse(argv)
    return Model.parse(argv)
  rescue RuntimeError => e
    raise if !e.message.start_with?("model:")
    Laws.unparsed << argv
    return nil
  end

  # The world the planner says the argv leaves, as keys: the command
  # line parsed by main's own parser into a Request, stepped from the
  # world before (judged, as the snapshot holds it), its plans
  # applied. nil when the snapshot carries no world, when the line is
  # not one main parses, or when -H names a stack the compiler cannot
  # build -- main refuses that before planning.
  def planned(argv, before)
    return nil if before.installs.nil?
    require_relative '../main'
    begin
      opts = Main.parse_options(argv.dup)
    rescue OptionParser::ParseError
      return nil
    end
    req = Main.request_of(opts)
    if req.stack
      gcc = pkgmgr.stack_compiler
      return nil if gcc.nil? || !gcc.installable_versions.include?(req.stack)
    end
    scope = Scope.env(stack: req.stack || pkgmgr.default_stack_cc_ver)
    out = Planner.step(pkgmgr, before.installs, req, scope)
    return Bridge.keys_of_world(out.world)
  end

  def worlds(before, after, expected, subject: "implementation",
             oracle: "model")
    fmt = ->(w) { w.map(&:to_s).sort.map { |s| "    #{s}" }.join("\n") }
    return [
      "  before:", fmt.call(before),
      "  #{subject}:", fmt.call(after),
      "  #{oracle}:", fmt.call(expected),
      "  only in #{subject}:", fmt.call(after - expected),
      "  only in #{oracle}:", fmt.call(expected - after),
    ].join("\n")
  end
end
