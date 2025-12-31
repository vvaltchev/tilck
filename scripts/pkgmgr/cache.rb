# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'progress'

require 'fileutils'
require 'net/http'
require 'uri'
require 'io/console'


# Extend instances of the URI::Generic (base class for URI::HTTP, URI:HTTPS
# etc.) with an operator + such that we do URI.join() with the given string
# and return a new URI.
#
# URI.join() handles both absolute and relative location.
module URI
  class Generic
    def +(loc) = URI.join(to_s, loc.to_s)
  end
end

module Cache

  module_function

  module Impl
    module_function

    MAX_HTTP_REDIRECT_COUNT = 10
    COMMON_HEADERS = {
      "User-Agent" => "Ruby/#{RUBY_VERSION} Net::HTTP",
      "Accept" => "*/*",
      "Accept-Encoding" => "identity", # Ask for true Content-Length
    }

    def do_actual_download(resp, local_path)

      total = 0.0
      last_update = 0.0
      expected = resp.content_length

      p = ProgressReporter.new(expected)

      File.open(local_path, "wb") do |f|
        resp.read_body do |chunk|
          f.write(chunk)
          total += chunk.length
          p.update(total)
        end
      end

      p.finish()

      if expected && total != expected
        puts "ERROR: downloaded #{total} B < expected #{expected}"
        return false
      end

      return true
    end

    def do_download_uri(uri, local_path, redirects)

      if redirects == 0
        puts "Download: #{uri}"
      end

      if redirects > MAX_HTTP_REDIRECT_COUNT
        puts "ERROR: redirect_count exceeded limit"
        return false
      end

      use_ssl = (uri.scheme == "https")
      Net::HTTP.start(uri.host, uri.port, use_ssl: use_ssl) do |http|

        req = Net::HTTP::Get.new(uri.request_uri, COMMON_HEADERS)
        http.request(req) do |resp|

          loc = resp["location"]
          case resp

            when Net::HTTPSuccess
              return do_actual_download(resp, local_path)

            when Net::HTTPRedirection

              if !loc.nil? && !loc.empty?
                return do_download_uri(uri + loc, local_path, redirects+1)
              end

              puts "ERROR: redirect with empty/nil location"

            else
              puts "ERROR: got #{resp.code}: #{resp.message}"

          end  # case resp
        end # do |resp|
      end # do |http|

      return false
    end

    def download_url(url, local_path)

      return do_download_uri(URI.parse(url), local_path, 0)

      rescue SignalException, Interrupt

        puts "" if STDOUT.tty?
        puts "*** Got signal or user interrupt. Stop. ***"

      ensure
        FileUtils.rm_f(local_path)
    end

  end # module Impl

  def download_file_in_cache(url, remote_file, local_file = nil)

    local_file ||= remote_file
    local_path = TC_CACHE / local_file

    # Both params must be file *names* not paths.
    assert { !remote_file.include? "/" }
    assert { !local_file.include? "/" }

    if File.file? local_path
      if local_file == remote_file
        puts "NOTE: Skipping the download of #{local_file}"
      else
        puts "NOTE: Skipping the download of #{local_file} (#{remote_file})"
      end
      return
    end

    puts "The file does not exist, download!"

    # Download here the file.
    success = Impl.download_url("#{url}/#{remote_file}", local_path)

    if !success
      puts "ERROR: Download failed"
    end
  end

end # module Cache
