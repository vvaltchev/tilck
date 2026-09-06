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
#                  outside the model's grammar.
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
    end

    if argv.include?("-d") && before.world != after.world
      out << Violation.new(:L2_dry_run, argv,
                           worlds(before.world, after.world, before.world))
    end

    for m in after.misplaced do
      out << Violation.new(:L3_placement, argv, m)
    end

    if !argv.include?("-d")
      touched = after.world - before.world
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

  def worlds(before, after, expected)
    fmt = ->(w) { w.map(&:to_s).sort.map { |s| "    #{s}" }.join("\n") }
    return [
      "  before:", fmt.call(before),
      "  implementation:", fmt.call(after),
      "  model:", fmt.call(expected),
      "  only in implementation:", fmt.call(after - expected),
      "  only in model:", fmt.call(expected - after),
    ].join("\n")
  end
end
