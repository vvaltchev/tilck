# SPDX-License-Identifier: BSD-2-Clause
#
# A postcondition is what must be true of an install once it is in
# place -- the behavioural half of expected_files. Three properties
# make it what it is: it runs ONCE, after the atomic move; it never
# runs on a scan; and it is never part of the recipe digest.
#

require_relative 'test_helper'
require 'stringio'

class TestPostconditions < Minitest::Test

  include TestHelper
  include Recipe::DSL

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  class Checked < TestHelper::FakePackage
    attr_accessor :checks
    def postconditions(ver = default_ver) = (@checks || [])
  end

  # A postcondition that records where it ran, and answers as told.
  class Probe < Postcondition::Base
    attr_reader :ran_in
    def initialize(ok) = (@ok = ok; @ran_in = [])
    def check(pkg, dir)
      @ran_in << dir.to_s
      return @ok
    end
  end

  def checked(name, *checks)
    p = Checked.new(name, on_host: true, host_tier: :distro)
    p.checks = checks
    pkgmgr.register(p)
    return p
  end

  def test_a_passing_install_stays_and_the_check_ran_where_it_lives
    with_fake_tc do
      with_stubbed_externals do
        probe = Probe.new(true)
        p = checked("host_fine", probe)

        assert pkgmgr.install("host_fine")
        pkgmgr.refresh

        inst = p.get_install_list.first
        refute_nil inst
        assert_equal [inst.path.to_s], probe.ran_in,
                     "checked against the FINAL directory, once"
      end
    end
  end

  # ...and a failing one installs nothing, the same as a failed build.
  def test_a_failing_install_is_taken_back_out
    with_fake_tc do
      with_stubbed_externals do
        p = checked("host_dud", Probe.new(false))

        out, = capture_io { refute pkgmgr.install("host_dud") }
        pkgmgr.refresh

        assert_empty p.get_install_list.map { |i| i.path.to_s }
        assert_match(/does not pass its own checks; removed/, out)
      end
    end
  end

  # Never on a scan: compiling a test program is not something -l does.
  def test_a_scan_does_not_run_postconditions
    with_fake_tc do
      with_stubbed_externals do
        p = checked("host_quiet", Probe.new(true))
        assert pkgmgr.install("host_quiet")

        later = Probe.new(false)
        p.checks = [later]
        pkgmgr.refresh
        pkgmgr.refresh

        assert_empty later.ran_in, "a scan ran a postcondition"
        inst = p.get_install_list.first
        refute_nil inst
        refute inst.broken
      end
    end
  end

  # Never in the digest: tightening a check must not invalidate an
  # artifact. Two packages with the same recipe and different checks
  # are the same recipe.
  def test_postconditions_are_not_part_of_the_recipe_digest
    with_fake_tc do
      a = checked("host_a", Probe.new(true))
      b = checked("host_b", Probe.new(false), Probe.new(false))
      [a, b].each { |p|
        p.define_singleton_method(:build_steps) { |v = nil|
          [Recipe::Run.new(log: "b.log", argv: ["make"])]
        }
      }
      assert_equal a.build_recipe_digest, b.build_recipe_digest
    end
  end

  # The one kind shipped: the installed program starts.
  def test_runs_passes_when_the_program_starts_and_says_so
    Dir.mktmpdir("pkgmgr-pc-") do |d|
      dir = Pathname.new(d)
      FileUtils.mkdir_p(dir / "install/bin")
      File.write(dir / "install/bin/tool", "#!/bin/sh\necho tool 9.9\n")
      FileUtils.chmod(0755, dir / "install/bin/tool")

      pkg = TestHelper::FakePackage.new("host_t", on_host: true)
      out, = capture_io {
        assert Postcondition::Runs.new(argv: ["install/bin/tool", "-v"])
                 .check(pkg, dir)
      }
      assert_match(/tool runs: tool 9.9/, out)
    end
  end

  def test_runs_fails_when_it_does_not_and_names_the_program
    Dir.mktmpdir("pkgmgr-pc-") do |d|
      dir = Pathname.new(d)
      pkg = TestHelper::FakePackage.new("host_t", on_host: true)

      out, = capture_io {
        refute Postcondition::Runs.new(argv: ["sh", "-c", "exit 3"])
                 .check(pkg, dir)
      }
      assert_match(/sh does not run \(exit 3\)/, out)

      out, = capture_io {
        refute Postcondition::Runs.new(argv: ["install/bin/absent"])
                 .check(pkg, dir)
      }
      assert_match(/absent cannot be run/, out)
    end
  end
end
