# SPDX-License-Identifier: BSD-2-Clause
#
# Tests for the Cache module: download_file, extract_file, and the
# HTTP download internals. Uses a real in-process HTTP server and
# real temp directories — no mocking of the cache code itself.
#

require_relative 'test_helper'
require_relative 'test_http_server'

class TestCacheDownloadFile < Minitest::Test
  include TestHelper

  def setup
    @server = TestHTTPServer.new
    @body = "fake tarball content for testing"
  end

  def teardown
    @server.stop
  end

  def test_download_success
    @server.route("/pkg-1.0.tar.gz") { |req|
      { status: 200, body: @body, content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "pkg-1.0.tar.gz")
      assert ok
      assert (tc / "cache" / "pkg-1.0.tar.gz").file?
      assert_equal @body, File.read(tc / "cache" / "pkg-1.0.tar.gz")
    end
  end

  def test_download_with_distinct_local_name
    @server.route("/remote.tar.gz") { |req|
      { status: 200, body: @body, content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "remote.tar.gz", "local.tar.gz")
      assert ok
      assert (tc / "cache" / "local.tar.gz").file?
      refute (tc / "cache" / "remote.tar.gz").exist?
    end
  end

  def test_download_skips_if_cached
    with_fake_tc do |tc|
      # Pre-create the file in cache
      FileUtils.touch(tc / "cache" / "pkg-1.0.tar.gz")

      # Server not even started — should not attempt download
      ok = Cache.download_file(@server.url, "pkg-1.0.tar.gz")
      assert ok
    end
  end

  def test_download_follows_redirect
    @server.route("/old-path.tar.gz") { |req|
      { status: 302, headers: { "Location" => "/new-path.tar.gz" } }
    }
    @server.route("/new-path.tar.gz") { |req|
      { status: 200, body: @body, content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "old-path.tar.gz")
      assert ok
      assert_equal @body, File.read(tc / "cache" / "old-path.tar.gz")
    end
  end

  def test_download_follows_multiple_redirects
    @server.route("/r1.tar.gz") { |req|
      { status: 301, headers: { "Location" => "/r2.tar.gz" } }
    }
    @server.route("/r2.tar.gz") { |req|
      { status: 302, headers: { "Location" => "/final.tar.gz" } }
    }
    @server.route("/final.tar.gz") { |req|
      { status: 200, body: @body, content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "r1.tar.gz")
      assert ok
      assert_equal @body, File.read(tc / "cache" / "r1.tar.gz")
    end
  end

  def test_download_404_fails
    @server.route("/missing.tar.gz") { |req|
      { status: 404, body: "Not Found" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "missing.tar.gz")
      refute ok
      refute (tc / "cache" / "missing.tar.gz").exist?
    end
  end

  def test_download_500_fails
    @server.route("/error.tar.gz") { |req|
      { status: 500, body: "Internal Server Error" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "error.tar.gz")
      refute ok
    end
  end

  def test_redirect_with_empty_location_fails
    @server.route("/bad-redirect.tar.gz") { |req|
      { status: 302, headers: {} }  # no Location header
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "bad-redirect.tar.gz")
      refute ok
    end
  end
end

class TestCacheDownloadEdgeCases < Minitest::Test
  include TestHelper

  def setup
    @server = TestHTTPServer.new
  end

  def teardown
    @server.stop
  end

  def test_content_length_mismatch_fails
    body = "short"
    @server.route("/mismatch.tar.gz") { |req|
      # Advertise 10000 bytes but only send 5
      {
        status: 200,
        body: body,
        content_type: "application/gzip",
        headers: { "Content-Length" => "10000" }
      }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "mismatch.tar.gz")
      refute ok
    end
  end

  def test_no_content_length_succeeds
    body = "data without content-length header"
    @server.route("/nolen.tar.gz") { |req|
      # Send data with Content-Length: 0 so Net::HTTP treats it as
      # unknown length. Actually, to get no Content-Length at all,
      # we use raw_handler.
      {
        raw_handler: ->(client) {
          client.print "HTTP/1.1 200 OK\r\n"
          client.print "Content-Type: application/gzip\r\n"
          client.print "Connection: close\r\n"
          client.print "\r\n"
          client.print body
        }
      }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "nolen.tar.gz")
      assert ok
      assert_equal body, File.read(tc / "cache" / "nolen.tar.gz")
    end
  end

  def test_redirect_loop_fails
    # Create a redirect that points to itself
    @server.route("/loop.tar.gz") { |req|
      { status: 302, headers: { "Location" => "/loop.tar.gz" } }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "loop.tar.gz")
      refute ok
    end
  end

  def test_redirect_chain_exceeds_limit
    # Create a chain of 12 redirects (limit is 10)
    (1..12).each do |i|
      next_path = i < 12 ? "/hop#{i+1}.tar.gz" : "/final.tar.gz"
      @server.route("/hop#{i}.tar.gz") { |req|
        { status: 302, headers: { "Location" => next_path } }
      }
    end
    @server.route("/final.tar.gz") { |req|
      { status: 200, body: "arrived", content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "hop1.tar.gz")
      refute ok
    end
  end

  def test_connection_drop_mid_transfer
    @server.route("/drop.tar.gz") { |req|
      {
        raw_handler: ->(client) {
          # Send headers claiming 10000 bytes, then only send 100
          # and close the connection.
          client.print "HTTP/1.1 200 OK\r\n"
          client.print "Content-Length: 10000\r\n"
          client.print "Content-Type: application/gzip\r\n"
          client.print "Connection: close\r\n"
          client.print "\r\n"
          client.print("x" * 100)
          # Socket closes here — client sees truncated transfer
        }
      }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "drop.tar.gz")
      refute ok
      # Partial file should NOT be left in cache
      refute (tc / "cache" / "drop.tar.gz").exist?
    end
  end

  def test_empty_200_response
    @server.route("/empty.tar.gz") { |req|
      { status: 200, body: "" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "empty.tar.gz")
      # Empty body with 200 — download_url returns true (no
      # content-length mismatch since both are 0), but the file
      # won't be useful. The download itself should succeed.
      assert ok
    end
  end

  def test_large_file_chunked_download
    # Create a ~500KB body to exercise the ProgressReporter
    body = "A" * (500 * 1024)
    @server.route("/large.tar.gz") { |req|
      { status: 200, body: body, content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "large.tar.gz")
      assert ok
      assert_equal body.bytesize,
                   File.size(tc / "cache" / "large.tar.gz")
    end
  end

  def test_redirect_to_absolute_url
    body = "redirected content"
    @server.route("/abs-redir.tar.gz") { |req|
      # Redirect to an absolute URL on the same server
      {
        status: 302,
        headers: {
          "Location" => "http://127.0.0.1:#{@server.port}/target.tar.gz"
        }
      }
    }
    @server.route("/target.tar.gz") { |req|
      { status: 200, body: body, content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "abs-redir.tar.gz")
      assert ok
      assert_equal body, File.read(tc / "cache" / "abs-redir.tar.gz")
    end
  end
end

class TestCacheExtractFile < Minitest::Test
  include TestHelper

  # Create a real .tar.gz containing a single directory with a file.
  def make_test_tarball(tc, tarname, inner_dir, filename = "hello.txt")
    Dir.mktmpdir do |staging|
      dir = File.join(staging, inner_dir)
      FileUtils.mkdir_p(dir)
      File.write(File.join(dir, filename), "test content")
      system("tar", "cfz", (tc / "cache" / tarname).to_s,
             "-C", staging, inner_dir)
    end
  end

  def test_extract_success
    with_fake_tc do |tc|
      make_test_tarball(tc, "pkg-1.0.tgz", "pkg-1.0")

      gcc = FAKE_GCC_VER.to_s
      dest = tc / "gcc-#{gcc}" / ARCH.name / "mypkg"
      FileUtils.mkdir_p(dest)

      FileUtils.cd(dest) do
        ok = Cache.extract_file("pkg-1.0.tgz", "1.0.0")
        assert ok
        assert (dest / "1.0.0" / "hello.txt").file?
      end
    end
  end

  def test_extract_renames_inner_dir
    with_fake_tc do |tc|
      # Tarball has "upstream-name-1.0/" but we want "1.0.0/"
      make_test_tarball(tc, "pkg-1.0.tgz", "upstream-name-1.0")

      dest = tc / "noarch" / "mypkg"
      FileUtils.mkdir_p(dest)

      FileUtils.cd(dest) do
        ok = Cache.extract_file("pkg-1.0.tgz", "1.0.0")
        assert ok
        assert (dest / "1.0.0" / "hello.txt").file?
        refute (dest / "upstream-name-1.0").exist?
      end
    end
  end

  def test_extract_default_dir_name
    with_fake_tc do |tc|
      make_test_tarball(tc, "pkg-1.0.tgz", "pkg-1.0")

      dest = tc / "noarch" / "mypkg"
      FileUtils.mkdir_p(dest)

      FileUtils.cd(dest) do
        ok = Cache.extract_file("pkg-1.0.tgz")  # no newDirName
        assert ok
        assert (dest / "pkg-1.0" / "hello.txt").file?
      end
    end
  end

  def test_extract_cleans_stale_tmp
    with_fake_tc do |tc|
      make_test_tarball(tc, "pkg-1.0.tgz", "pkg-1.0")

      # Create a stale tmp dir in cache
      FileUtils.mkdir_p(tc / "cache" / "tmp")
      File.write(tc / "cache" / "tmp" / "stale", "old")

      dest = tc / "noarch" / "mypkg"
      FileUtils.mkdir_p(dest)

      FileUtils.cd(dest) do
        ok = Cache.extract_file("pkg-1.0.tgz", "1.0.0")
        assert ok
        # Stale tmp should be cleaned up
        refute (tc / "cache" / "tmp").exist?
      end
    end
  end

  def test_extract_outside_toolchain_fails
    with_fake_tc do |tc|
      make_test_tarball(tc, "pkg-1.0.tgz", "pkg-1.0")

      # Try to extract from a directory NOT under TC
      Dir.mktmpdir do |outside|
        FileUtils.cd(outside) do
          ok = Cache.extract_file("pkg-1.0.tgz", "1.0.0")
          refute ok
        end
      end
    end
  end
end

class TestCacheDownloadAndExtractIntegration < Minitest::Test
  include TestHelper

  def setup
    @server = TestHTTPServer.new
  end

  def teardown
    @server.stop
  end

  # Create a real tarball in memory and serve it via HTTP.
  def make_tarball_bytes(inner_dir, filename = "deliverable")
    Dir.mktmpdir do |staging|
      dir = File.join(staging, inner_dir)
      FileUtils.mkdir_p(dir)
      File.write(File.join(dir, filename), "built output")

      tarpath = File.join(staging, "archive.tar.gz")
      system("tar", "cfz", tarpath, "-C", staging, inner_dir)
      return File.binread(tarpath)
    end
  end

  def test_download_then_extract
    tarball = make_tarball_bytes("upstream-1.0", "built_binary")

    @server.route("/upstream-1.0.tar.gz") { |req|
      { status: 200, body: tarball, content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      # Download
      ok = Cache.download_file(@server.url, "upstream-1.0.tar.gz",
                               "mypkg-1.0.tar.gz")
      assert ok
      assert (tc / "cache" / "mypkg-1.0.tar.gz").file?

      # Extract
      dest = tc / "noarch" / "mypkg"
      FileUtils.mkdir_p(dest)

      FileUtils.cd(dest) do
        ok = Cache.extract_file("mypkg-1.0.tar.gz", "1.0.0")
        assert ok
        assert (dest / "1.0.0" / "built_binary").file?
        assert_equal "built output",
                     File.read(dest / "1.0.0" / "built_binary")
      end
    end
  end

  def test_download_redirect_then_extract
    tarball = make_tarball_bytes("src-2.0")

    @server.route("/old.tar.gz") { |req|
      { status: 302, headers: { "Location" => "/new.tar.gz" } }
    }
    @server.route("/new.tar.gz") { |req|
      { status: 200, body: tarball, content_type: "application/gzip" }
    }
    @server.start

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "old.tar.gz")
      assert ok

      dest = tc / "noarch" / "pkg"
      FileUtils.mkdir_p(dest)

      FileUtils.cd(dest) do
        ok = Cache.extract_file("old.tar.gz", "2.0.0")
        assert ok
        assert (dest / "2.0.0").directory?
      end
    end
  end
end
