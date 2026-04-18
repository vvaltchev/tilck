# SPDX-License-Identifier: BSD-2-Clause
#
# Tests for resumable downloads. Uses the test HTTP server with
# Range request support to verify all resume scenarios.
#

require_relative 'test_helper'
require_relative 'test_http_server'

class TestResumeDownload < Minitest::Test
  include TestHelper

  BODY = "A" * 1000 + "B" * 1000 + "C" * 1000  # 3000 bytes

  def setup
    @server = TestHTTPServer.new
  end

  def teardown
    @server.stop
  end

  # Helper: set up a server route that supports Range requests.
  def serve_with_range(path, body)
    @server.route(path) { |req|
      range = req[:headers]["range"]

      if range && range =~ /\Abytes=(\d+)-/
        offset = $1.to_i

        if offset >= body.bytesize
          # Range not satisfiable
          { status: 416, body: "Range Not Satisfiable" }
        else
          slice = body[offset..]
          {
            status: 206,
            body: slice,
            content_type: "application/octet-stream",
            headers: {
              "Content-Range" =>
                "bytes #{offset}-#{body.bytesize - 1}/#{body.bytesize}",
            }
          }
        end
      else
        # No Range header → full download
        {
          status: 200,
          body: body,
          content_type: "application/octet-stream"
        }
      end
    }
    @server.start
  end

  # --- Fresh download (no partial file) ---

  def test_fresh_download_creates_partial_then_moves
    serve_with_range("/file.tar.gz", BODY)

    with_fake_tc do |tc|
      ok = Cache.download_file(@server.url, "file.tar.gz")
      assert ok

      # Final file should exist
      assert (tc / "cache" / "file.tar.gz").file?
      assert_equal BODY, File.read(tc / "cache" / "file.tar.gz")

      # Partial file should be gone (moved to final)
      refute (tc / "cache" / "partial" / "file.tar.gz").exist?
    end
  end

  # --- Resume after partial download ---

  def test_resume_from_partial_file
    serve_with_range("/file.tar.gz", BODY)

    with_fake_tc do |tc|
      # Pre-create a partial file with the first 1000 bytes
      partial_dir = tc / "cache" / "partial"
      FileUtils.mkdir_p(partial_dir)
      File.write(partial_dir / "file.tar.gz", BODY[0, 1000])

      ok = Cache.download_file(@server.url, "file.tar.gz")
      assert ok

      # Final file should have the complete content
      final = File.read(tc / "cache" / "file.tar.gz")
      assert_equal BODY, final
      assert_equal BODY.bytesize, final.bytesize

      # Partial should be cleaned up
      refute (partial_dir / "file.tar.gz").exist?
    end
  end

  # --- Server doesn't support resume (returns 200 instead of 206) ---

  def test_no_resume_support_restarts_download
    # Server always returns 200, ignoring Range header
    @server.route("/file.tar.gz") { |req|
      { status: 200, body: BODY, content_type: "application/octet-stream" }
    }
    @server.start

    with_fake_tc do |tc|
      # Pre-create partial file
      partial_dir = tc / "cache" / "partial"
      FileUtils.mkdir_p(partial_dir)
      File.write(partial_dir / "file.tar.gz", "old partial data")

      ok = Cache.download_file(@server.url, "file.tar.gz")
      assert ok

      # Should have the complete fresh download
      assert_equal BODY, File.read(tc / "cache" / "file.tar.gz")
    end
  end

  # --- Resume with redirect ---

  def test_resume_through_redirect
    @server.route("/old.tar.gz") { |req|
      { status: 302, headers: { "Location" => "/new.tar.gz" } }
    }
    serve_with_range("/new.tar.gz", BODY)

    with_fake_tc do |tc|
      # Partial file for old.tar.gz
      partial_dir = tc / "cache" / "partial"
      FileUtils.mkdir_p(partial_dir)
      File.write(partial_dir / "old.tar.gz", BODY[0, 500])

      ok = Cache.download_file(@server.url, "old.tar.gz")
      assert ok
      assert_equal BODY, File.read(tc / "cache" / "old.tar.gz")
    end
  end

  # --- Range not satisfiable (416) — corrupt partial, restart ---

  def test_416_restarts_download
    serve_with_range("/file.tar.gz", BODY)

    with_fake_tc do |tc|
      # Partial file is LARGER than the remote — triggers 416
      partial_dir = tc / "cache" / "partial"
      FileUtils.mkdir_p(partial_dir)
      File.write(partial_dir / "file.tar.gz", "x" * (BODY.bytesize + 100))

      ok = Cache.download_file(@server.url, "file.tar.gz")
      assert ok
      assert_equal BODY, File.read(tc / "cache" / "file.tar.gz")
    end
  end

  # --- Connection drop mid-download, then resume ---

  def test_drop_then_resume
    call_count = 0

    @server.route("/file.tar.gz") { |req|
      call_count += 1
      range = req[:headers]["range"]
      offset = 0

      if range && range =~ /\Abytes=(\d+)-/
        offset = $1.to_i
      end

      if call_count == 1 && offset == 0
        # First request: send only 500 bytes then drop
        {
          raw_handler: ->(client) {
            client.print "HTTP/1.1 200 OK\r\n"
            client.print "Content-Length: #{BODY.bytesize}\r\n"
            client.print "Accept-Ranges: bytes\r\n"
            client.print "Content-Type: application/octet-stream\r\n"
            client.print "Connection: close\r\n"
            client.print "\r\n"
            client.print BODY[0, 500]
            # Close — simulates connection drop
          }
        }
      else
        # Subsequent request: serve from offset
        if offset > 0 && offset < BODY.bytesize
          slice = BODY[offset..]
          {
            status: 206,
            body: slice,
            content_type: "application/octet-stream",
            headers: {
              "Content-Range" =>
                "bytes #{offset}-#{BODY.bytesize - 1}/#{BODY.bytesize}",
            }
          }
        else
          { status: 200, body: BODY,
            content_type: "application/octet-stream" }
        end
      end
    }
    @server.start

    with_fake_tc do |tc|
      # First attempt — will get a partial download (500 bytes)
      # then content-length mismatch → failure
      ok = Cache.download_file(@server.url, "file.tar.gz")
      refute ok

      # Partial file should exist with 500 bytes
      partial = tc / "cache" / "partial" / "file.tar.gz"
      assert partial.file?
      assert_equal 500, File.size(partial)

      # Second attempt — should resume from byte 500
      ok = Cache.download_file(@server.url, "file.tar.gz")
      assert ok

      # Complete file
      assert_equal BODY, File.read(tc / "cache" / "file.tar.gz")
      refute partial.exist?
    end
  end

  # --- Skips download if final file already exists ---

  def test_skip_if_already_cached
    with_fake_tc do |tc|
      FileUtils.touch(tc / "cache" / "file.tar.gz")

      # Server not started — should not attempt download
      ok = Cache.download_file(@server.url, "file.tar.gz")
      assert ok
    end
  end

  # --- Distinct local name with resume ---

  def test_resume_with_distinct_local_name
    serve_with_range("/remote.tar.gz", BODY)

    with_fake_tc do |tc|
      # Partial file uses the LOCAL name
      partial_dir = tc / "cache" / "partial"
      FileUtils.mkdir_p(partial_dir)
      File.write(partial_dir / "local.tar.gz", BODY[0, 1500])

      ok = Cache.download_file(@server.url, "remote.tar.gz", "local.tar.gz")
      assert ok

      assert_equal BODY, File.read(tc / "cache" / "local.tar.gz")
      refute (partial_dir / "local.tar.gz").exist?
    end
  end
end
