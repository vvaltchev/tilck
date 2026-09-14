# SPDX-License-Identifier: BSD-2-Clause
#
# SEVERAL PACKAGE MANAGERS ON ONE TREE AT ONCE.
#
# Real processes (tests/parallel/worker.rb), a real throwaway tree
# under a TCROOT_PARENT of the test's, real tarballs served over HTTP
# by the test: everything two package managers can contend for is
# contended for here -- the download's partial file, the cache file
# and its pin check, the cache's record, the temporary directory, the
# staging directory and the move into place -- and the tree at the
# end must be the one a single package manager would have left.
#
# Half the workers install the SAME package, and hold its build long
# enough that the rest arrive while it is held: exactly one of them
# builds, the others wait and find it installed. The other half each
# install a package of their own at the same moment: every one is
# installed, every cache file is recorded, and the record is whole.
#
# Nothing here is Linux's: flock, kill 0 and mkdir are the same on
# FreeBSD and macOS, and the test is meant to be run there.
#
require_relative 'test_helper'
require_relative 'test_http_server'
require 'rbconfig'
require 'tmpdir'

class TestParallelPackageManagers < Minitest::Test
  include TestHelper

  WORKER = File.join(__dir__, "parallel", "worker.rb")

  def setup
    @server = TestHTTPServer.new
  end

  def teardown
    @server.stop
  end

  # A real tar.gz of one directory holding one file with `content`.
  def tarball(content)
    Dir.mktmpdir do |d|
      FileUtils.mkdir_p(File.join(d, "1.0"))
      File.write(File.join(d, "1.0", "src.c"), content)
      out = File.join(d, "t.tar.gz")
      system("tar", "cfz", out, "-C", d, "1.0") or raise "tar failed"
      return File.binread(out)
    end
  end

  def serve(file, bytes)
    @server.route("/#{file}") { |_req|
      { status: 200, body: bytes, content_type: "application/gzip" }
    }
    return pin_of(bytes)
  end

  # One package manager in its own process, its output kept. TCROOT
  # is named outright: the wrapper script exports it, a worker would
  # inherit it, and TCROOT_PARENT alone yields to it -- the first run
  # of this test inside the suite installed its fakes into the real
  # tree. The worker refuses a tree outside `parent` as well.
  def worker(parent, name, file, pin, delay)
    log = File.join(parent, "#{name}-#{rand(1 << 30)}.log")
    env = { "TCROOT_PARENT" => parent,
            "TCROOT" => File.join(parent, TC.basename.to_s),
            "ARCH" => "i386" }
    pid = Process.spawn(env, RbConfig.ruby, WORKER, name, @server.url,
                        file, pin.to_s, delay.to_s,
                        out: log, err: [:child, :out])
    return [pid, log]
  end

  def test_several_package_managers_on_one_tree
    same_pin = serve("same-1.0.tar.gz", tarball("same"))
    # Host packages, so that no cross compiler is asked for: named as
    # host packages are, installed under the name less the prefix.
    own = (1..4).map { |i|
      ["host_own#{i}", "own#{i}-1.0.tar.gz", serve("own#{i}-1.0.tar.gz",
                                                   tarball("own #{i}"))]
    }
    @server.start

    Dir.mktmpdir("pkgmgr-parallel-") do |parent|
      tc = Pathname(parent) / TC.basename
      FileUtils.mkdir_p(tc / "cache")

      jobs = (1..4).map { worker(parent, "host_same", "same-1.0.tar.gz",
                                 same_pin, 1.5) }
      jobs += own.map { |name, file, pin|
        worker(parent, name, file, pin, 0.2)
      }

      results = jobs.map { |pid, log|
        Process.wait(pid)
        [$?.exitstatus, File.read(log)]
      }
      failed = results.reject { |rc, _| rc == 0 }
      assert_empty failed.map { |rc, out| "rc=#{rc}\n#{out}" },
                   "a package manager failed"

      # Exactly one built the shared package; the others waited for
      # the build and found it done.
      same_logs = results.first(4).map(&:last)
      built = same_logs.count { |o|
        o.include?("Installed package host_same")
      }
      assert_equal 1, built, same_logs.join("\n----\n")
      assert_equal 3, same_logs.count { |o|
        o.include?("Installed by another package manager while waiting")
      }, same_logs.join("\n----\n")
      assert same_logs.any? { |o| o.include?("Waiting for another") }

      pkgs = tc / "linux-x86_64" / "any" / "any" / "pkgs"
      by = File.read(pkgs / "same" / "1.0" / "built-by")
      assert_match(/\A\d+\n\z/, by)
      assert_equal 1, Dir.children(pkgs / "same").length
      for name, _, _ in own do
        dir = name.delete_prefix("host_")
        assert (pkgs / dir / "1.0" / "built-by").file?, name
      end

      # The cache holds every file once, as its pin names it, and the
      # record names every one: no entry lost to another's write.
      files = ["same-1.0.tar.gz"] + own.map { |_, f, _| f }
      for f in files do
        assert (tc / "cache" / f).file?, f
      end
      record = Table.read(tc / "cache" / ".hashes")
      assert_equal files.sort, record.keys.sort
      assert_equal same_pin.to_s, record["same-1.0.tar.gz"]
      for name, f, pin in own do
        assert_equal pin.to_s, record[f], name
      end

      # Nothing left behind: no temporary directory, no partial, no
      # file set aside, no staging.
      left = Dir.children(tc / "cache").reject { |e|
        files.include?(e) || e == ".hashes" || e == Lock::DIR
      }
      assert_equal ["partial"], left
      assert_empty Dir.children(tc / "cache" / "partial")
      assert_empty Dir.glob("#{tc}/staging/*/")
    end
  end

  # A temporary directory left by a package manager that died is
  # swept; one whose owner runs is another's, and kept.
  def test_a_dead_package_manager_s_temporary_directory_is_swept
    with_fake_tc do |tc|
      # A pid that was a process a moment ago, on any host: spawned
      # and reaped, not guessed above a pid_max that differs per OS.
      dead = Process.spawn(RbConfig.ruby, "-e", "exit")
      Process.wait(dead)
      FileUtils.mkdir_p(tc / "cache" / "tmp.#{dead}")
      FileUtils.mkdir_p(tc / "cache" / "tmp.#{Process.pid}")
      FileUtils.mkdir_p(tc / "cache" / "tmp")
      live = Process.spawn(RbConfig.ruby, "-e", "sleep 30")
      FileUtils.mkdir_p(tc / "cache" / "tmp.#{live}")
      begin
        out = capture_output { Cache.sweep_stale_tmp }
        assert_match(/tmp\.#{dead}/, out)
        refute (tc / "cache" / "tmp.#{dead}").exist?
        refute (tc / "cache" / "tmp").exist?, "the old shared directory"
        assert (tc / "cache" / "tmp.#{Process.pid}").exist?
        assert (tc / "cache" / "tmp.#{live}").exist?
      ensure
        Process.kill("TERM", live)
        Process.wait(live)
      end
    end
  end

  # The lock itself: exclusive holders take turns, shared holders do
  # not wait on each other, and a waiter says so.
  def test_a_lock_is_one_holder_s_at_a_time
    with_fake_tc do |tc|
      dir = (tc / "cache" / Lock::DIR).to_s
      order = []
      t = Thread.new {
        Lock.held(dir, "x") { order << :a_in; sleep 0.3; order << :a_out }
      }
      sleep 0.1
      out = capture_output {
        Lock.held(dir, "x", what: "x") { order << :b_in }
      }
      t.join
      assert_equal [:a_in, :a_out, :b_in], order
      assert_match(/Waiting for another package manager: x/, out)

      # Two readers at once: the second is in while the first still
      # holds, and is not told it waited.
      both = []
      r1 = Thread.new {
        Lock.held(dir, "y", shared: true) { both << 1; sleep 0.6 }
      }
      sleep 0.1
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      Lock.held(dir, "y", shared: true) { |waited|
        both << [2, waited, r1.alive?]
      }
      assert_operator Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0,
                      :<, 0.4
      r1.join
      assert_equal [1, [2, false, true]], both

      # ...and a writer is told, and waits for every reader; the
      # message names the lock when nothing better was given.
      r2 = Thread.new { Lock.held(dir, "z", shared: true) { sleep 0.3 } }
      sleep 0.1
      seen = nil
      out = capture_output { Lock.held(dir, "z") { |waited| seen = waited } }
      r2.join
      assert_equal true, seen
      assert_match(/Waiting for another package manager: z$/, out)
    end
  end
end
