# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'progress'

require 'fileutils'
require 'tmpdir'
require 'net/http'
require 'uri'
require 'io/console'
require 'open3'

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

# Export global environment variables to make the `git` tool behave in a way
# that make sense in this context.
ENV["GIT_TERMINAL_PROMPT"] = "0"
ENV["GIT_ADVICE"] = "0"

module Cache

  extend FileShortcuts
  extend FileUtilsShortcuts

  module_function

  module Impl
    extend FileShortcuts
    extend FileUtilsShortcuts

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
        error "Downloaded #{total} B < expected #{expected}"
        return false
      end

      return true
    end

    def do_download_uri(uri, local_path, redirects)

      if redirects == 0
        info "Download: #{uri}"
      end

      if redirects > MAX_HTTP_REDIRECT_COUNT
        error "Redirect_count exceeded limit"
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

              error "Redirect with empty/nil location"

            else
              error "Got #{resp.code}: #{resp.message}"

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
        rm_f(local_path)

      rescue
        rm_f(local_path)
    end

    # Thin wrappers around the git command. Tests replace these to
    # simulate git behavior without a real repo.
    def run_git(*args) = system("git", *args)
    def capture_git(*args) = Open3.capture2("git", *args)

    def git_clone(url, destdir, tag)

      if tag.nil?
        ok = run_git("clone", "--depth", "1", url, destdir)
        raise LocalError, "Failed to clone git repo: #{url}" if !ok
        return true
      end

      ok = run_git("clone", "--branch", tag, "--depth", "1", url, destdir)
      return true if ok

       # Git clone failed. There could be several reasons for that:
       #
       #     - The remote git server is down
       #
       #     - The pointed branch/tag/commit does not exist anymore because
       #       the ref has been deleted or the history has been rewritten.
       #
       #     - In some corner cases, fetching individual untagged commits
       #       is not allowed. It's worth retrying with a full clone only
       #       if the tag looks like a hex commit SHA.
       #       See: https://stackoverflow.com/a/51002078/2077198
       #
      raise LocalError, "Failed to clone git repo: #{url}" if
        !tag.match?(/\A[0-9a-f]+\z/)

      # We failed to clone the repo, but the tag is a git SHA (corner case 3),
      # so it's worth trying a workaround.
      ok = run_git("clone", url, destdir)
      raise LocalError, "Failed to clone git repo: #{url}" if !ok

      # OK, a regular full-clone succeeded. Now let's checkout the specific
      # commit, if it exists.
      chdir(destdir) do
        ok = run_git("checkout", tag)
        raise LocalError, "Failed to checkout tag: #{tag}" if !ok

        # OK, we succeeded. Now, let's save the commit info before we delete
        # the .git directory to save space.
        out, status = capture_git("rev-parse", "--abbrev-ref", "HEAD")
        raise LocalError, "Git rev-parse failed" if !status.success?
        File.write(".ref_name", out)

        out, status = capture_git("rev-parse", "--short", "HEAD")
        raise LocalError, "Git rev-parse failed" if !status.success?
        File.write(".ref_short", out)

        out, status = capture_git("rev-parse", "HEAD")
        raise LocalError, "Git rev-parse failed" if !status.success?
        File.write(".ref", out)
      end

      return true # success

    rescue LocalError => e
      error e
      return false
    end # git_clone()
  end # module Impl

  def download_file(url, remote_file, local_file = nil)

    local_file ||= remote_file
    local_path = TC_CACHE / local_file

    # Both params must be file *names* not paths.
    assert { !remote_file.include? "/" }
    assert { !local_file.include? "/" }

    if file? local_path
      if local_file == remote_file
        info "Skipping the download of #{local_file}"
      else
        info "Skipping the download of #{local_file} (#{remote_file})"
      end
      return true
    end

    # Download here the file.
    success = Impl.download_url("#{url}/#{remote_file}", local_path)

    if !success
      error "Download failed"
      rm_f(local_path)  # don't leave partial files in cache
    end

    return success
  end

  def extract_file(tarfile, newDirName = nil)

    extToOpt = {
      ".gz" => "xfz",
      ".tgz" => "xfz",
      ".bz2" => "xfj",
      ".xz" => "xfJ",
    }

    filepath = (TC_CACHE / tarfile).to_s()
    assert { exist? filepath }

    opt = extToOpt[extname(tarfile)]
    assert { !opt.nil? }
    tmp = TC_CACHE / "tmp"

    if exist? tmp
      warning "cache tmp directory exists: #{tmp}"
      warning "deleting directory #{tmp}"
      puts
      rm_rf(tmp)
    end

    mkdir(tmp)
    current_dir = mkpathname(getwd()).realpath()
    info "extract #{tarfile} in #{current_dir}/"
    tc_real = TC.realpath()

    if ! current_dir.ascend.any? { |p| p == tc_real }
      raise LocalError, "Current dir is not in the toolchain"
    end

    chdir(tmp) do
      tar_argv = ["tar", opt, filepath]

      # macOS: archives built by libarchive (macOS /usr/bin/tar) embed
      # pax extended headers for Apple-specific xattrs such as
      # com.apple.provenance. GNU tar doesn't recognise them and
      # prints a warning per entry. Silence those.
      if OS == "Darwin"
        tar_argv << "--warning=no-unknown-keyword"
      end

      ok = system(*tar_argv)
      raise LocalError, "Tar extract failed" if !ok

      contents = Dir.children(".")

      # macOS: gtar may emit AppleDouble resource-fork entries (._*)
      # alongside the real directory. Remove them so only the real
      # content remains.
      dot_us = contents.select { |e| e.start_with?("._") }
      dot_us.each { |e| rm_rf(e) }
      contents -= dot_us

      raise LocalError, "The archive #{tarfile} is empty" if
        contents.length == 0

      if contents.length > 1
        error "the archive #{tarfile} has multiple subdirs:"
        error contents.join "\n"
        puts
        raise LocalError, "Multiple subdirs not supported"
      end

      dirname = contents[0]
      newDirName ||= dirname
      mv(tmp / dirname, current_dir / newDirName)
    end

    return true

  rescue LocalError => e
    error e
    return false

  ensure
    rm_rf(tmp)
  end # extract_file()

  def download_git_repo(
    url,                 # git repo URL
    tarname,             # tarname in the cache
    tag = nil,           # git tag or branch to use
    dir_name = nil       # dir name to use inside the archive
  )

    assert { tarname.end_with? ".tgz" }
    filepath_in_cache = TC_CACHE / tarname
    tmp = TC_CACHE / "tmp"
    dir_name ||= tag

    # The dir name cannot contain a path separator char.
    assert { dir_name.nil? or dir_name.index("/").nil? }

    if filepath_in_cache.file?
      tagstr = tag.nil?? "" : ", tag: #{tag}"
      info "Skipping git clone of: #{url}#{tagstr}"
      return true
    end

    if exist? tmp
      warning "cache tmp directory exists: #{tmp}"
      warning "deleting directory #{tmp}"
      puts
      rm_rf(tmp)
    end

    mkdir(tmp)
    chdir(tmp) do

      ok = Impl.git_clone(url, dir_name, tag)
      return false if !ok

      contents = Dir.children(".")

      # After the git clone, we expect to see exactly one directory here.
      assert { contents.length == 1 }

      # Either we don't know the dir_name or it's exactly what we expect.
      assert { dir_name.nil? or contents[0] == dir_name }

      info "Packaging #{tarname} in the cache"
      ok = system("tar", "cfz", tarname, contents[0])
      raise LocalError, "Failed to pack cloned git repo" if !ok

      assert { mkpathname(tarname).file? }
      mv(tarname, filepath_in_cache)
    end

    return true

  rescue LocalError => e
    error e
    return false
  ensure
    rm_rf(tmp)
  end # download_git_repo()

end # module Cache
