# SPDX-License-Identifier: BSD-2-Clause
#
# Minimal HTTP server for testing Cache download logic. Runs in a
# background thread on a random port. No external gems needed —
# uses only Ruby stdlib (socket).
#
# Usage:
#   server = TestHTTPServer.new
#   server.route("/file.tar.gz") { |req|
#     { status: 200, body: file_content, content_type: "application/gzip" }
#   }
#   server.route("/redirect") { |req|
#     { status: 302, headers: { "Location" => "/file.tar.gz" } }
#   }
#   server.start
#   # ... run tests against "http://127.0.0.1:#{server.port}/..." ...
#   server.stop

require 'socket'

class TestHTTPServer

  attr_reader :port

  def initialize
    @server = TCPServer.new("127.0.0.1", 0)
    @port = @server.addr[1]
    @routes = {}
    @thread = nil
    @running = false
  end

  # Register a handler for a path. The block receives a request hash
  # { method:, path:, headers: } and returns a response hash:
  #   { status:, body:, headers:, content_type: }
  def route(path, &handler)
    @routes[path] = handler
  end

  def start
    @running = true
    @thread = Thread.new { serve_loop }
  end

  def stop
    @running = false
    @server.close rescue nil
    @thread&.join(2)
  end

  def url(path = "")
    "http://127.0.0.1:#{@port}#{path}"
  end

  private

  def serve_loop
    while @running
      client = @server.accept rescue break
      Thread.new(client) { |c| handle_client(c) }
    end
  end

  def handle_client(client)
    request_line = client.gets
    return if request_line.nil?

    method, path, _ = request_line.split(" ", 3)

    # Read headers
    headers = {}
    while (line = client.gets) && line.strip != ""
      key, val = line.split(":", 2)
      headers[key.strip.downcase] = val.strip if key && val
    end

    req = { method: method, path: path, headers: headers }
    handler = @routes[path]

    if handler
      resp = handler.call(req)
    else
      resp = { status: 404, body: "Not Found: #{path}" }
    end

    send_response(client, resp)
  rescue => e
    # Silently ignore connection errors during shutdown
  ensure
    client.close rescue nil
  end

  def send_response(client, resp)
    status = resp[:status] || 200
    body = resp[:body] || ""
    content_type = resp[:content_type] || "text/plain"
    extra_headers = resp[:headers] || {}

    status_text = {
      200 => "OK",
      301 => "Moved Permanently",
      302 => "Found",
      404 => "Not Found",
      500 => "Internal Server Error",
    }[status] || "Unknown"

    lines = ["HTTP/1.1 #{status} #{status_text}"]
    lines << "Content-Type: #{content_type}"
    lines << "Content-Length: #{body.bytesize}" if !body.empty?

    extra_headers.each { |k, v| lines << "#{k}: #{v}" }

    lines << "Connection: close"
    lines << ""

    client.print(lines.join("\r\n") + "\r\n")
    client.print(body) if !body.empty?
  end
end
