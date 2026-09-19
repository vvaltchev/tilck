# SPDX-License-Identifier: BSD-2-Clause
#
# Tests for Cache::Impl.git_clone and Cache.download_git_repo using
# a mock git layer. The production code calls run_git/capture_git
# (thin wrappers around system/Open3) and waits between retries via
# wait_before_retry; tests replace all three, so that every git
# scenario can be simulated without a real remote repo and without
# the suite ever sleeping through a backoff.
#

require_relative 'test_helper'

# Mock git behavior. Each test configures a MockGit instance and
# installs it on Cache::Impl for the duration of the test.
#
# clone_handler: receives (args_array, destdir) → true/false.
#   When returning true, must create the destdir with content.
# checkout_ok:   whether `git checkout` succeeds.
# rev_parse_ok:  whether `git rev-parse` succeeds.
#
class MockGit

  attr_accessor :clone_handler, :checkout_ok, :rev_parse_ok
  attr_accessor :commit  # what rev-parse HEAD answers
  attr_reader :calls  # log of [method, args] for assertions
  attr_reader :waits  # the backoff delays the retries asked for

  COMMIT = "0123456789abcdef0123456789abcdef01234567"

  def initialize
    @clone_handler = nil
    @checkout_ok = true
    @rev_parse_ok = true
    @commit = COMMIT
    @calls = []
    @waits = []
  end

  def pin = SourcePins.commit(@commit)

  # The pause between two attempts: recorded, never slept through.
  def wait(secs)
    @waits << secs
  end

  def run(*args)
    @calls << [:run, args]
    cmd = args[0]

    if cmd == "clone"
      # Find the destdir: last arg unless it's a flag or the url.
      # Real git derives it from the url when not specified.
      url_idx = args.index { |a| a =~ /\Ahttps?:/ || a =~ /\Agit:/ }
      destdir = args.last
      if destdir.nil? || destdir == args[url_idx]
        # No explicit dest — derive from URL like real git
        destdir = File.basename(args[url_idx].to_s, ".git")
      end

      if @clone_handler
        return @clone_handler.call(args, destdir)
      end
      # Default: succeed, create the dir with a dummy file
      FileUtils.mkdir_p(destdir)
      File.write(File.join(destdir, "README"), "mock content")
      return true
    end

    if cmd == "checkout"
      return @checkout_ok
    end

    return true
  end

  def capture(*args)
    @calls << [:capture, args]
    if @rev_parse_ok
      out = args.include?("--short") ? @commit[0, 7] : @commit
      return [out + "\n", mock_status(true)]
    else
      return ["", mock_status(false)]
    end
  end

  private
  def mock_status(success)
    s = Object.new
    s.define_singleton_method(:success?) { success }
    s
  end
end

module MockGitHelper

  # Install a MockGit on Cache::Impl for the duration of a block.
  def with_mock_git(mock = MockGit.new)
    orig_run = Cache::Impl.method(:run_git)
    orig_cap = Cache::Impl.method(:capture_git)
    orig_wait = Cache::Impl.method(:wait_before_retry)

    Cache::Impl.define_singleton_method(:run_git) { |*a| mock.run(*a) }
    Cache::Impl.define_singleton_method(:capture_git) { |*a| mock.capture(*a) }
    Cache::Impl.define_singleton_method(:wait_before_retry) { |s| mock.wait(s) }

    yield mock

  ensure
    Cache::Impl.define_singleton_method(:run_git, orig_run)
    Cache::Impl.define_singleton_method(:capture_git, orig_cap)
    Cache::Impl.define_singleton_method(:wait_before_retry, orig_wait)
  end
end

# ---------------------------------------------------------------
# Tests for Cache::Impl.git_clone
# ---------------------------------------------------------------

class TestGitCloneBasic < Minitest::Test
  include TestHelper
  include MockGitHelper

  def test_clone_no_tag_success
    with_mock_git do |mock|
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          ok = Cache::Impl.git_clone("https://fake/repo", "mydir", nil)
          assert ok
          assert File.directory?("mydir")

          # Should have called clone with --depth 1, no --branch
          clone_call = mock.calls.find { |m, a| m == :run && a[0] == "clone" }
          assert clone_call
          assert_includes clone_call[1], "--depth"
          refute_includes clone_call[1], "--branch"
        end
      end
    end
  end

  def test_clone_with_tag_success
    with_mock_git do |mock|
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          ok = Cache::Impl.git_clone("https://fake/repo", "mydir", "v1.0")
          assert ok
          assert File.directory?("mydir")

          clone_call = mock.calls.find { |m, a| m == :run && a[0] == "clone" }
          assert_includes clone_call[1], "--branch"
          assert_includes clone_call[1], "v1.0"
        end
      end
    end
  end

  def test_clone_no_tag_failure
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) { false }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          ok = Cache::Impl.git_clone("https://fake/repo", "mydir", nil)
          refute ok
        end
      end
    end
  end

  def test_clone_with_non_sha_tag_failure
    clones = []
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      clones << args
      false  # all clones fail
    }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          # "v1.0" is not a hex SHA, so there is no full-clone fallback
          ok = Cache::Impl.git_clone("https://fake/repo", "mydir", "v1.0")
          refute ok

          # Every attempt asked for the same ref: the clone is retried
          # (see TestGitCloneNetRetries), never widened into a full one.
          refute_empty clones
          assert clones.all? { |a| a.include?("--branch") }
        end
      end
    end
  end
end

# ---------------------------------------------------------------
# The retries: an upstream git server that resets a connection or
# times out a handshake has said nothing about the repository, so a
# single failed attempt is not an answer.
# ---------------------------------------------------------------

class TestGitCloneNetRetries < Minitest::Test
  include TestHelper
  include MockGitHelper

  def test_no_wait_when_the_first_attempt_works
    with_mock_git do |mock|
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          assert Cache::Impl.git_clone("https://fake/repo", "mydir", "v1.0")
          assert_empty mock.waits
        end
      end
    end
  end

  def test_a_failed_attempt_is_retried
    seen = []
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      seen << dest
      next false if seen.length == 1
      FileUtils.mkdir_p(dest)
      File.write(File.join(dest, "README"), "content")
      true
    }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          assert Cache::Impl.git_clone("https://fake/repo", "mydir", "v1.0")
          assert_equal 2, seen.length
          assert_equal [Cache::Impl::NET_RETRY_DELAYS.first], mock.waits
        end
      end
    end
  end

  # The retry policy itself, in one place: three attempts, two and
  # then eight seconds apart.
  def test_gives_up_after_the_last_attempt
    clones = 0
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      clones += 1
      false
    }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          refute Cache::Impl.git_clone("https://fake/repo", "mydir", "v1.0")
          assert_equal 3, clones
          assert_equal [2, 8], mock.waits
        end
      end
    end
  end

  def test_a_retry_starts_from_a_clean_slate
    found = []
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      found << Dir.exist?(dest)
      FileUtils.mkdir_p(File.join(dest, ".git"))  # half-cloned junk
      found.length > 1
    }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          assert Cache::Impl.git_clone("https://fake/repo", "mydir", "v1.0")

          # The second attempt found the destination as empty as the
          # first one did: git refuses to clone into a directory that
          # a dead attempt left behind.
          assert_equal [false, false], found
        end
      end
    end
  end

  def test_the_sha_shaped_tag_is_probed_only_once
    clones = []
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      clones << args
      next false if clones.length < 3
      FileUtils.mkdir_p(dest)
      File.write(File.join(dest, "file.c"), "code")
      true
    }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          ok = Cache::Impl.git_clone(
            "https://fake/repo", "mydir", "abcdef123456"
          )
          assert ok

          # git rejects the same SHA every time, so `--branch <sha>`
          # is asked once and once only...
          assert_equal 1, clones.count { |a| a.include?("--branch") }

          # ...while the full clone that follows it -- the request
          # that actually matters -- is retried like any other.
          assert_equal 3, clones.length
          assert_equal [Cache::Impl::NET_RETRY_DELAYS.first], mock.waits
        end
      end
    end
  end
end

class TestGitCloneSHARetry < Minitest::Test
  include TestHelper
  include MockGitHelper

  def test_sha_tag_retry_full_clone_succeeds
    attempt = 0
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      attempt += 1
      if attempt == 1
        # First attempt (--branch sha) fails
        false
      else
        # Second attempt (full clone) succeeds
        FileUtils.mkdir_p(dest)
        File.write(File.join(dest, "file.c"), "code")
        true
      end
    }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          ok = Cache::Impl.git_clone(
            "https://fake/repo", "mydir", "abcdef123456"
          )
          assert ok
          assert_equal 2, attempt
          # The clone is checked out; what it is at is the packer's
          # to ask (download_git_repo), not the clone's to write.
          refute File.exist?("mydir/.ref")
        end
      end
    end
  end

  def test_sha_tag_full_clone_also_fails
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) { false }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          ok = Cache::Impl.git_clone(
            "https://fake/repo", "mydir", "abcdef123456"
          )
          refute ok
        end
      end
    end
  end

  def test_sha_tag_checkout_fails
    attempt = 0
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      attempt += 1
      if attempt == 1
        false  # --branch fails
      else
        FileUtils.mkdir_p(dest)
        File.write(File.join(dest, "file.c"), "code")
        true  # full clone succeeds
      end
    }
    mock.checkout_ok = false  # but checkout fails

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          ok = Cache::Impl.git_clone(
            "https://fake/repo", "mydir", "abcdef123456"
          )
          refute ok
        end
      end
    end
  end

  def test_a_clone_git_cannot_place_is_not_packed
    mock = MockGit.new
    mock.rev_parse_ok = false

    with_fake_tc do |tc|
      with_mock_git(mock) do
        out = capture_output {
          refute Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                         "v1.0", "v1.0", pin: mock.pin)
        }
        assert_match(/rev-parse failed/, out)
        refute (tc / "cache" / "repo-1.0.tgz").exist?
      end
    end
  end
end

# ---------------------------------------------------------------
# Tests for Cache.download_git_repo (the higher-level function)
# ---------------------------------------------------------------

class TestDownloadGitRepo < Minitest::Test
  include TestHelper
  include MockGitHelper

  # A pack in the cache as the packer leaves one: the tree under
  # `dir`, saying its commit, no .git.
  def make_pack(tc, name, dir, commit: MockGit::COMMIT, ref: true)
    Dir.mktmpdir do |staging|
      d = File.join(staging, dir)
      FileUtils.mkdir_p(d)
      FileUtils.mkdir_p(File.join(d, ".git")) if !ref
      File.write(File.join(d, "README"), "content")
      File.write(File.join(d, ".ref"), commit + "\n") if ref
      system("tar", "cfz", (tc / "cache" / name).to_s, "-C", staging, dir)
    end
  end

  def clones_in(mock) = mock.calls.count { |m, a| m == :run && a[0] == "clone" }

  def test_skip_if_cached
    with_fake_tc do |tc|
      make_pack(tc, "repo-1.0.tgz", "1.0")
      out = capture_output {
        assert Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                       "v1.0", "1.0", pin: MockGit.new.pin)
      }
      assert_match(/Skipping git clone/, out)
      # ...recorded as it stands, for the next look.
      assert_equal SourcePins.digest_of(tc / "cache" / "repo-1.0.tgz").to_s,
                   Cache::Hashes.of("repo-1.0.tgz")
    end
  end

  def test_a_cached_pack_at_another_commit_is_set_aside_and_cloned_again
    with_fake_tc do |tc|
      make_pack(tc, "repo-1.0.tgz", "v1.0", commit: "f" * 40)
      with_mock_git do |mock|
        out = capture_output {
          assert Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                         "v1.0", "v1.0", pin: mock.pin)
        }
        assert_match(/set aside as rejected\/repo-1.0.tgz: not what/, out)
        assert (tc / "cache" / "rejected" / "repo-1.0.tgz").file?
        assert_equal MockGit::COMMIT,
                     Cache::Pack.member(tc / "cache" / "repo-1.0.tgz",
                                        ".ref").strip
      end
    end
  end

  def test_a_pack_from_before_packs_said_their_commit_is_cloned_again
    with_fake_tc do |tc|
      make_pack(tc, "repo-1.0.tgz", "v1.0", ref: false)
      with_mock_git do |mock|
        out = capture_output {
          assert Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                         "v1.0", "v1.0", pin: mock.pin)
        }
        assert_match(/set aside/, out)
        assert_equal 1, clones_in(mock)
      end
    end
  end

  def test_a_pack_from_before_and_no_pin_says_to_delete_it
    with_fake_tc do |tc|
      make_pack(tc, "repo-1.0.tgz", "v1.0", ref: false)
      with_mock_git do |mock|
        out = capture_output {
          refute Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                         "v1.0", "v1.0", pin: nil)
        }
        assert_match(/no pin for repo-1.0.tgz/, out)
        assert_match(/made before packs said their commit/, out)
        assert_equal 0, clones_in(mock)
      end
    end
  end

  def test_clone_and_package_success
    with_fake_tc do |tc|
      with_mock_git do |mock|
        # download_git_repo clones, then packages with tar
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-1.0.tgz", "v1.0", "v1.0", pin: mock.pin
        )
        assert ok
        pack = tc / "cache" / "repo-1.0.tgz"
        assert pack.file?
        assert_equal SourcePins.digest_of(pack).to_s,
                     Cache::Hashes.of("repo-1.0.tgz")
        # The pack says its commit, in full and as git abbreviates it,
        # and carries no history.
        assert_equal MockGit::COMMIT, Cache::Pack.member(pack, ".ref").strip
        assert_equal MockGit::COMMIT[0, 7],
                     Cache::Pack.member(pack, ".ref_short").strip
        names = `tar tzf #{pack}`.lines.map(&:strip)
        assert_includes names, "v1.0/README"
        refute names.any? { |n| n.include?("/.git/") }
      end
    end
  end

  def test_a_clone_that_carried_history_is_packed_without_it
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      FileUtils.mkdir_p(File.join(dest, ".git", "objects"))
      File.write(File.join(dest, ".git", "HEAD"), "ref: refs/heads/x\n")
      File.write(File.join(dest, "src.c"), "code")
      true
    }
    with_fake_tc do |tc|
      with_mock_git(mock) do
        assert Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                       "v1.0", "v1.0", pin: mock.pin)
        names = `tar tzf #{tc / "cache" / "repo-1.0.tgz"}`.lines.map(&:strip)
        assert_equal ["v1.0/", "v1.0/.ref", "v1.0/.ref_short", "v1.0/src.c"],
                     names.sort
      end
    end
  end

  def test_a_clone_at_another_commit_than_pinned_is_not_kept
    with_fake_tc do |tc|
      with_mock_git do |mock|
        out = capture_output {
          refute Cache.download_git_repo(
            "https://fake/repo", "repo-1.0.tgz", "v1.0", "v1.0",
            pin: SourcePins.commit("e" * 40)
          )
        }
        assert_match(/at v1.0 is at commit #{MockGit::COMMIT}/, out)
        assert_match(/pinned: git:#{"e" * 40}/, out)
        refute (tc / "cache" / "repo-1.0.tgz").exist?
        refute (tc / "cache" / "tmp").exist?
      end
    end
  end

  def test_an_unpinned_clone_is_packed_and_the_line_printed
    with_fake_tc do |tc|
      with_mock_git do |mock|
        out = capture_output {
          refute Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                         "v1.0", "v1.0", pin: nil)
        }
        assert_match(/no pin for repo-1.0.tgz/, out)
        assert_match(/ repo-1.0.tgz: git:#{MockGit::COMMIT}$/, out)
        assert (tc / "cache" / "repo-1.0.tgz").file?
        assert_nil Cache::Hashes.of("repo-1.0.tgz")

        # Pinned afterwards: the pack stands without another clone.
        n = clones_in(mock)
        assert Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                       "v1.0", "v1.0", pin: mock.pin)
        assert_equal n, clones_in(mock)
        refute_nil Cache::Hashes.of("repo-1.0.tgz")
      end
    end
  end

  def test_a_pack_takes_only_a_commit_pin
    with_fake_tc do |tc|
      out = capture_output {
        refute Cache.download_git_repo("https://fake/repo", "repo-1.0.tgz",
                                       "v1.0", "v1.0", pin: pin_of("x"))
      }
      assert_match(/its pin must be a commit/, out)
    end
  end

  def test_clone_failure_returns_false
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) { false }

    with_fake_tc do |tc|
      with_mock_git(mock) do
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-1.0.tgz", "v1.0", "v1.0", pin: mock.pin
        )
        refute ok
        # Cache file should NOT be created
        refute (tc / "cache" / "repo-1.0.tgz").exist?
      end
    end
  end

  def test_cleans_stale_tmp
    with_fake_tc do |tc|
      # Create a stale tmp dir
      FileUtils.mkdir_p(tc / "cache" / "tmp" / "old_stuff")

      with_mock_git do |mock|
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-1.0.tgz", "v1.0", "v1.0", pin: mock.pin
        )
        assert ok
        # tmp should be cleaned up
        refute (tc / "cache" / "tmp").exist?
      end
    end
  end

  def test_default_dir_name_from_tag
    with_fake_tc do |tc|
      with_mock_git do |mock|
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-1.0.tgz", "v1.0", pin: mock.pin
          # dir_name omitted → defaults to tag
        )
        assert ok

        # The clone should have been called with destdir = "v1.0"
        clone_call = mock.calls.find { |m, a| m == :run && a[0] == "clone" }
        assert_equal "v1.0", clone_call[1].last
      end
    end
  end

  def test_clone_no_tag
    with_fake_tc do |tc|
      with_mock_git do |mock|
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-1.0.tgz", pin: mock.pin
          # tag = nil, dir_name = nil
        )
        assert ok
        assert (tc / "cache" / "repo-1.0.tgz").file?
      end
    end
  end

  def test_a_pack_is_extracted_only_when_it_is_what_the_pin_names
    with_fake_tc do |tc|
      make_pack(tc, "repo-1.0.tgz", "1.0")
      dest = noarch_pkgs / "repo"
      FileUtils.mkdir_p(dest)
      FileUtils.cd(dest) do
        out = capture_output {
          refute Cache.extract_file("repo-1.0.tgz", "1.0.0",
                                    pin: SourcePins.commit("e" * 40))
        }
        assert_match(/not what other\/pkg_hashes names/, out)
        refute (dest / "1.0.0").exist?
        assert Cache.extract_file("repo-1.0.tgz", "1.0.0", pin: MockGit.new.pin)
        assert (dest / "1.0.0" / "README").file?
      end
    end
  end

  def test_sha_tag_full_flow
    # Simulate: --branch clone fails, full clone + checkout succeeds
    attempt = 0
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      attempt += 1
      if attempt == 1
        false  # --branch fails for SHA
      else
        FileUtils.mkdir_p(dest)
        File.write(File.join(dest, "src.c"), "code")
        true
      end
    }

    with_fake_tc do |tc|
      with_mock_git(mock) do
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-sha.tgz",
          "abcdef123456", "abcdef123456", pin: mock.pin
        )
        assert ok
        assert (tc / "cache" / "repo-sha.tgz").file?
      end
    end
  end
end
