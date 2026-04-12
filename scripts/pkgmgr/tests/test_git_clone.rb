# SPDX-License-Identifier: BSD-2-Clause
#
# Tests for Cache::Impl.git_clone and Cache.download_git_repo using
# a mock git layer. The production code calls run_git/capture_git
# (thin wrappers around system/Open3); tests replace those to
# simulate various git scenarios without a real remote repo.
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
  attr_reader :calls  # log of [method, args] for assertions

  def initialize
    @clone_handler = nil
    @checkout_ok = true
    @rev_parse_ok = true
    @calls = []
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
      return ["abc123\n", mock_status(true)]
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

    Cache::Impl.define_singleton_method(:run_git) { |*a| mock.run(*a) }
    Cache::Impl.define_singleton_method(:capture_git) { |*a| mock.capture(*a) }

    yield mock

  ensure
    Cache::Impl.define_singleton_method(:run_git, orig_run)
    Cache::Impl.define_singleton_method(:capture_git, orig_cap)
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
    call_count = 0
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      call_count += 1
      false  # all clones fail
    }

    with_mock_git(mock) do
      Dir.mktmpdir do |dir|
        FileUtils.cd(dir) do
          # "v1.0" is not a hex SHA, so no retry
          ok = Cache::Impl.git_clone("https://fake/repo", "mydir", "v1.0")
          refute ok
          # Only one clone attempt (--branch), no full-clone retry
          assert_equal 1, call_count
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
          # Should have written .ref files
          assert File.file?("mydir/.ref_name")
          assert File.file?("mydir/.ref_short")
          assert File.file?("mydir/.ref")
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

  def test_sha_tag_rev_parse_fails
    attempt = 0
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) {
      attempt += 1
      if attempt == 1
        false
      else
        FileUtils.mkdir_p(dest)
        File.write(File.join(dest, "file.c"), "code")
        true
      end
    }
    mock.rev_parse_ok = false

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
end

# ---------------------------------------------------------------
# Tests for Cache.download_git_repo (the higher-level function)
# ---------------------------------------------------------------

class TestDownloadGitRepo < Minitest::Test
  include TestHelper
  include MockGitHelper

  def test_skip_if_cached
    with_fake_tc do |tc|
      # Pre-create the cached tarball
      FileUtils.touch(tc / "cache" / "repo-1.0.tgz")

      ok = Cache.download_git_repo(
        "https://fake/repo", "repo-1.0.tgz", "v1.0", "1.0"
      )
      assert ok
    end
  end

  def test_clone_and_package_success
    with_fake_tc do |tc|
      with_mock_git do
        # download_git_repo clones, then packages with tar
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-1.0.tgz", "v1.0", "v1.0"
        )
        assert ok
        assert (tc / "cache" / "repo-1.0.tgz").file?
      end
    end
  end

  def test_clone_failure_returns_false
    mock = MockGit.new
    mock.clone_handler = ->(args, dest) { false }

    with_fake_tc do |tc|
      with_mock_git(mock) do
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-1.0.tgz", "v1.0", "v1.0"
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

      with_mock_git do
        ok = Cache.download_git_repo(
          "https://fake/repo", "repo-1.0.tgz", "v1.0", "v1.0"
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
          "https://fake/repo", "repo-1.0.tgz", "v1.0"
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
          "https://fake/repo", "repo-1.0.tgz"
          # tag = nil, dir_name = nil
        )
        assert ok
        assert (tc / "cache" / "repo-1.0.tgz").file?
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
          "abcdef123456", "abcdef123456"
        )
        assert ok
        assert (tc / "cache" / "repo-sha.tgz").file?
      end
    end
  end
end
