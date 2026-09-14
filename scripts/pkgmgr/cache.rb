# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'progress'
require_relative 'source_pins'
require_relative 'cache_hashes'

require 'fileutils'
require 'tmpdir'
require 'uri'
require 'io/console'
require 'open3'
require 'zlib'
require 'rubygems/package'

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

# A build runs under the toolchain, which sits inside this checkout,
# and an upstream build that asks git about its own tree -- a version
# banner from `git describe` -- must not be answered by ours: a pack
# carries no .git, so git would walk up and find Tilck's, and
# micropython once printed our commit as its version. Discovery stops
# at the toolchain root; a clone in the cache has its .git where it
# starts, and is not affected.
ENV["GIT_CEILING_DIRECTORIES"] = TC.to_s

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

    # Download body into local_path, appending if resuming.
    # offset: bytes already downloaded (0 for fresh start).
    def do_actual_download(resp, local_path, offset = 0)

      total = offset.to_f
      expected_total = nil

      if resp.content_length
        expected_total = offset + resp.content_length
      end

      p = ProgressReporter.new(expected_total)
      p.update(total) if offset > 0

      mode = offset > 0 ? "ab" : "wb"
      File.open(local_path, mode) do |f|
        resp.read_body do |chunk|
          f.write(chunk)
          total += chunk.length
          p.update(total)
        end
      end

      p.finish()

      if expected_total && total != expected_total
        error "Downloaded #{total.to_i} B < expected #{expected_total.to_i}"
        return false
      end

      return true
    end

    # Follow redirects and download. Supports resume via Range header.
    # partial_path: path to the partial file (nil = no resume).
    def do_download_uri(uri, local_path, redirects,
                        partial_path: nil, partial_size: 0)

      # Required here, not at the top: net/http drags in resolv and
      # socket for about 10 ms, and every operation that does not
      # fetch anything -- which is most of them, since the cache
      # exists precisely so that builds are offline -- was paying it.
      # 'uri' stays up there: URI::Generic is reopened at load.
      require 'net/http'

      if redirects == 0
        if partial_size > 0
          info "Resuming download: #{uri} (#{partial_size} bytes already)"
        else
          info "Download: #{uri}"
        end
      end

      if redirects > MAX_HTTP_REDIRECT_COUNT
        error "Redirect_count exceeded limit"
        return false
      end

      use_ssl = (uri.scheme == "https")
      Net::HTTP.start(uri.host, uri.port, use_ssl: use_ssl) do |http|

        headers = COMMON_HEADERS.dup
        if partial_size > 0
          headers["Range"] = "bytes=#{partial_size}-"
        end

        req = Net::HTTP::Get.new(uri.request_uri, headers)
        http.request(req) do |resp|

          loc = resp["location"]
          case resp

            when Net::HTTPPartialContent  # 206 — resume accepted
              return do_actual_download(resp, local_path, partial_size)

            when Net::HTTPSuccess  # 200 — full response
              if partial_size > 0
                # Server doesn't support resume — start fresh
                warning "Server does not support resume, restarting"
                rm_f(local_path)
              end
              return do_actual_download(resp, local_path, 0)

            when Net::HTTPRedirection
              if !loc.nil? && !loc.empty?
                return do_download_uri(
                  uri + loc, local_path, redirects + 1,
                  partial_path: partial_path,
                  partial_size: partial_size
                )
              end
              error "Redirect with empty/nil location"

            when Net::HTTPRequestedRangeNotSatisfiable  # 416
              # Range not satisfiable — partial file is corrupt or
              # larger than the remote. Delete and start fresh.
              warning "Range not satisfiable, restarting download"
              rm_f(local_path)
              return do_download_uri(
                uri, local_path, redirects,
                partial_path: nil, partial_size: 0
              )

            else
              error "Got #{resp.code}: #{resp.message}"

          end  # case resp
        end # do |resp|
      end # do |http|

      return false
    end

    # Top-level download entry. Manages partial file lifecycle.
    #
    # On interrupt or error the partial file is preserved for resume.
    # On success the partial file is moved to the final location.
    def download_url(url, final_path)

      partial_dir = File.join(File.dirname(final_path), "partial")
      FileUtils.mkdir_p(partial_dir)

      partial_path = File.join(partial_dir, File.basename(final_path))
      partial_size = File.exist?(partial_path) ? File.size(partial_path) : 0

      success = do_download_uri(
        URI.parse(url), partial_path, 0,
        partial_path: partial_path,
        partial_size: partial_size
      )

      if success
        mv(partial_path, final_path)
      end

      return success

    rescue SignalException, Interrupt
      puts "" if STDOUT.tty?
      puts "*** Got signal or user interrupt. Stop. ***"
      # Partial file is preserved for resume on next run.
      return nil

    rescue => e
      error "Download error: #{e.message}"
      # Partial file is preserved for resume on next run.
      return nil
    end

    # Thin wrappers around the git command. Tests replace these to
    # simulate git behavior without a real repo.
    def run_git(*args) = system("git", *args)
    def capture_git(*args) = Open3.capture2("git", *args)

    # Replaced by the tests as well: a suite that really waited would
    # pay the full backoff of every retry it exercises.
    def wait_before_retry(secs) = sleep(secs)

    # How long to wait before each retry of a network operation. The
    # number of attempts is one more than the number of delays, so the
    # two cannot disagree. The first pause is short because most of
    # what it covers is a single reset; the second is longer because a
    # server that failed us twice needs a moment, not another hit.
    #
    # An upstream git server can be slow rather than broken:
    # git.musl-libc.org answers a 5 KB request in anywhere between
    # half a second and forty, resetting the connection in between,
    # and one failed attempt is not an answer about the repository.
    #
    NET_RETRY_DELAYS = [2, 8].freeze

    # Run the block until it returns true, waiting NET_RETRY_DELAYS
    # between the attempts. `what` names the operation for the user.
    #
    # The block receives the attempt number: only the operation knows
    # what starting over means for itself.
    def with_net_retries(what)

      attempts = NET_RETRY_DELAYS.length + 1

      for i in 1..attempts do

        return true if yield(i)
        break if i == attempts

        delay = NET_RETRY_DELAYS[i - 1]
        warning "#{what} failed (attempt #{i}/#{attempts}): " \
                "retrying in #{delay} seconds"
        wait_before_retry(delay)
      end

      return false
    end

    # One `git clone`, attempted more than once.
    #
    # An attempt that died half-way can leave its destination behind
    # (git removes it when it fails on its own, not when it is killed)
    # and git refuses to clone into a non-empty directory: the retry
    # has to start from the same clean slate the first attempt had.
    def git_clone_retrying(url, destdir, *opts)

      with_net_retries("Cloning #{url}") do |attempt|
        rm_rf(destdir) if attempt > 1
        run_git("clone", *opts, url, destdir)
      end
    end

    def git_clone(url, destdir, tag)

      if tag.nil?
        ok = git_clone_retrying(url, destdir, "--depth", "1")
        raise LocalError, "Failed to clone git repo: #{url}" if !ok
        return true
      end

      shallow_opts = ["--branch", tag, "--depth", "1"]

      # A tag that is a branch or a tag name is fetched directly, and
      # failing to do so is worth retrying: the ref either exists or
      # it does not, so what makes the same request fail twice in a
      # row is the remote git server, not the ref.
      #
      # A tag that looks like a hex commit SHA is a different story:
      # in some corner cases, fetching individual untagged commits is
      # not allowed, so `--branch <sha>` is expected to fail and the
      # full clone below is the request that actually matters. It
      # gets one optimistic shot and no retries: git refuses the same
      # SHA every time, and making every commit-pinned package wait
      # out the backoff would only teach the reader to ignore the
      # warning. See: https://stackoverflow.com/a/51002078/2077198
      #
      if !tag.match?(/\A[0-9a-f]+\z/)
        ok = git_clone_retrying(url, destdir, *shallow_opts)
        raise LocalError, "Failed to clone git repo: #{url}" if !ok
        return true
      end

      return true if run_git("clone", *shallow_opts, url, destdir)

      # The shallow shot failed and the tag is a git SHA, so it's
      # worth trying the workaround: a full clone.
      ok = git_clone_retrying(url, destdir)
      raise LocalError, "Failed to clone git repo: #{url}" if !ok

      # OK, a regular full-clone succeeded. Now let's checkout the specific
      # commit, if it exists.
      chdir(destdir) do
        ok = run_git("checkout", tag)
        raise LocalError, "Failed to checkout tag: #{tag}" if !ok
      end

      return true # success

    rescue LocalError => e
      error e
      return false
    end # git_clone()

    # The commit a clone is at -- in full, or as git abbreviates it
    # with `short: true` -- or nil where git cannot say.
    def head_of(destdir, short: false)
      args = ["rev-parse", *(short ? ["--short"] : []), "HEAD"]
      out, status = chdir(destdir) { capture_git(*args) }
      return status.success? ? out.strip : nil
    end
  end # module Impl

  #
  # THE ARCHIVE A CLONED SOURCE IS PACKED INTO, owned here and nowhere
  # else: the extension a packed name ends with, how a tree becomes
  # one, and how one member is read back without unpacking the rest.
  # A pack is ours, so nothing about it is promised to stay -- a
  # better compression is a change to these three -- and that is why
  # a packed source is pinned by its commit and not by the archive's
  # digest (SourcePins).
  #
  module Pack
    EXT = ".tgz"

    module_function

    # `dir`, under the current directory, as the archive `out`.
    def create(dir, out) = system("tar", "cfz", out, dir)

    # The content of `basename` at the top of the packed tree, or nil
    # where the archive has no such member or is no archive of ours.
    def member(path, basename)
      Zlib::GzipReader.open(path.to_s) do |gz|
        Gem::Package::TarReader.new(gz) do |tar|
          want = %r{\A[^/]+/#{Regexp.escape(basename)}\z}
          tar.each do |e|
            return e.read if e.file? && e.full_name =~ want
          end
        end
      end
      return nil
    rescue Zlib::Error, Gem::Package::TarInvalidError, IOError, EOFError
      return nil
    end
  end

  # The commit a packed clone came from, written at the top of its
  # tree by the packer and read back by the check: what makes a pack
  # say for itself which source it is.
  REF = ".ref"
  REF_SHORT = ".ref_short"

  # Where a file in the cache is set aside when it is not what it
  # should be, for a person to look at; fetched again in its place.
  REJECTED = "rejected"

  #
  # WHERE A CACHED FILE STANDS against `pin`, what other/pkg_hashes
  # names for it (Hashes.judge): its bytes now, what the cache
  # recorded when it placed them and, for a pack, the commit it says
  # it came from. `kind` is what the caller fetches -- :sha256 for a
  # file downloaded as it is, :git for a pack -- so that a pack is
  # asked for its commit even before it has a pin.
  #
  Look = Data.define(:state, :digest, :ref)

  def look(name, pin, kind:)
    path = TC_CACHE / name
    digest = SourcePins.digest_of(path)
    ref = kind == :git ? SourcePins.commit(Pack.member(path, REF)) : nil
    st = Hashes.judge(pin: pin, digest: digest, recorded: Hashes.of(name),
                      ref: ref&.value)
    return Look.new(state: st, digest: digest, ref: ref)
  end

  # What the cached file is, as the line that would pin it: the
  # commit a pack says, else the digest.
  def observed_line(name, look) = SourcePins.line(name, look.ref || look.digest)

  # The file looked at and, when it stands, recorded if the cache
  # had not yet: what every use of a cached file does first, quietly,
  # so that a file checked once is held to its bytes from then on.
  def vouch(name, pin, kind:)
    l = look(name, pin, kind: kind)
    Hashes.record(name, l.digest) if l.state == :ok && Hashes.of(name).nil?
    return l
  end

  # A file that stands is accepted; one that does not is explained
  # and refused, left where it is.
  def accept(name, pin, look, kind:)
    case look.state
    when :ok
      return true
    when :unpinned
      error "no pin for #{name} in other/pkg_hashes"
      if kind == :git && look.ref.nil?
        info "the cached pack was made before packs said their commit:"
        info "delete #{TC_CACHE / name} and run again"
      else
        info "the file in the cache is:"
        info "   #{observed_line(name, look)}"
        info "add that line to other/pkg_hashes if it is the source " \
             "you mean, and run again"
      end
    when :damaged
      error "#{name} in the cache is damaged: its bytes are not the " \
            "ones recorded when it was placed (#{Hashes::FILE})"
    when :other
      error "#{name} in the cache is not what other/pkg_hashes names"
      info "   pinned: #{pin}"
      info "   cached: #{look.ref || look.digest}"
    end
    return false
  end

  def verified?(name, pin, kind:)
    return accept(name, pin, vouch(name, pin, kind: kind), kind: kind)
  end

  # The file out of the way, under REJECTED, and out of the record:
  # what is fetched next takes its name.
  def set_aside(name, look, pin)
    why = look.state == :damaged ? "its bytes moved since it was placed" :
            "not what other/pkg_hashes names (#{pin})"
    dir = TC_CACHE / REJECTED
    mkdir_p(dir)
    mv(TC_CACHE / name, dir / name)
    Hashes.forget(name)
    warning "#{name} set aside as #{REJECTED}/#{name}: #{why}"
  end

  # A downloaded file is what `pin` names, or it is not kept as
  # such: a wrong one is set aside with its digest shown against the
  # pin's, an unpinned one is left in place, unrecorded, with the
  # line to add printed. One already in the cache is looked at the
  # same way, and fetched again when it is not what it should be.
  def download_file(url, remote_file, local_file = nil, pin:)

    local_file ||= remote_file
    local_path = TC_CACHE / local_file

    # The local name becomes a path under the cache and must stay a
    # bare filename. The remote one may carry a relative subpath,
    # because some projects publish each release in its own directory
    # (GCC: gnu/gcc/gcc-<ver>/gcc-<ver>.tar.xz); that is a detail of
    # the remote layout and never reaches the cache, which is why such
    # a source must give an explicit local name.
    assert { !local_file.include? "/" }
    assert { !remote_file.start_with?("/") }
    assert { !remote_file.include?("..") }

    if pin && pin.kind != :sha256
      error "#{local_file} is downloaded as it is: its pin must be a " \
            "sha256, not #{pin}"
      return false
    end

    if file? local_path
      l = vouch(local_file, pin, kind: :sha256)
      if l.state == :ok || l.state == :unpinned
        if l.state == :ok && local_file == remote_file
          info "Skipping the download of #{local_file}"
        elsif l.state == :ok
          info "Skipping the download of #{local_file} (#{remote_file})"
        end
        return accept(local_file, pin, l, kind: :sha256)
      end
      set_aside(local_file, l, pin)
    end

    # Download here the file.
    success = Impl.download_url("#{url}/#{remote_file}", local_path)

    if !success
      error "Download failed" if success == false
      # Partial file stays in cache/partial/ for resume.
      # Don't delete it — that's the whole point.
      return false
    end

    l = vouch(local_file, pin, kind: :sha256)
    ok = accept(local_file, pin, l, kind: :sha256)
    set_aside(local_file, l, pin) if l.state == :other
    return ok
  end

  # The archive extracted into the current directory, its top-level
  # directory renamed to newDirName -- once it has been checked to be
  # what `pin` names, bytes and all: an archive is read right before
  # it is built, and this is the last moment a wrong one can be
  # refused.
  def extract_file(tarfile, newDirName = nil, pin:)

    extToOpt = {
      ".gz" => "xfz",
      ".tgz" => "xfz",
      ".bz2" => "xfj",
      ".xz" => "xfJ",
    }
    # Our own packs are extracted here too: a change of pack format
    # (Pack) is a change of this table.
    assert { extToOpt.key?(Pack::EXT) }

    filepath = (TC_CACHE / tarfile).to_s()
    assert { exist? filepath }

    kind = pin&.kind == :git ? :git : :sha256
    return false if !verified?(tarfile, pin, kind: kind)

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
      dest = current_dir / newDirName
      # An upstream tree keeps an empty directory where a submodule
      # goes: the archive takes its place rather than landing inside.
      Dir.rmdir(dest) if dest.directory? && Dir.empty?(dest)
      mv(tmp / dirname, dest)
    end

    return true

  rescue LocalError => e
    error e
    return false

  ensure
    rm_rf(tmp) if tmp
  end # extract_file()

  # A cloned source, packed: the clone is what `pin` names or it is
  # not packed at all; the pack says its commit in REF at the top of
  # its tree and carries no .git, which is history the build does not
  # read and the largest part of the clone. One already in the cache
  # is looked at the same way a downloaded file is.
  def download_git_repo(
    url,                 # git repo URL
    tarname,             # tarname in the cache
    tag = nil,           # git tag or branch to use
    dir_name = nil,      # dir name to use inside the archive
    pin:                 # what other/pkg_hashes names, or nil
  )

    filepath_in_cache = TC_CACHE / tarname
    tmp = TC_CACHE / "tmp"
    dir_name ||= tag

    # The dir name cannot contain a path separator char.
    assert { dir_name.nil? or dir_name.index("/").nil? }

    if pin && pin.kind != :git
      error "#{tarname} is packed from a clone: its pin must be a " \
            "commit, not #{pin}"
      return false
    end

    if filepath_in_cache.file?
      l = vouch(tarname, pin, kind: :git)
      if l.state == :ok || l.state == :unpinned
        tagstr = tag.nil?? "" : ", tag: #{tag}"
        info "Skipping git clone of: #{url}#{tagstr}" if l.state == :ok
        return accept(tarname, pin, l, kind: :git)
      end
      set_aside(tarname, l, pin)
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

      head = Impl.head_of(contents[0])
      short = Impl.head_of(contents[0], short: true)
      raise LocalError, "Git rev-parse failed" if head.nil? || short.nil?

      if pin && head != pin.value
        error "#{url}#{tag ? " at #{tag}" : ""} is at commit #{head}"
        info "   pinned: #{pin}"
        info "the tag moved, or the pin is wrong: nothing was kept"
        return false
      end

      # The commit in full for the check, and as git abbreviates it
      # for a recipe that prints it ($SRC_REF).
      File.write(File.join(contents[0], REF), head + "\n")
      File.write(File.join(contents[0], REF_SHORT), short + "\n")
      rm_rf(File.join(contents[0], ".git"))

      info "Packaging #{tarname} in the cache"
      ok = Pack.create(contents[0], tarname)
      raise LocalError, "Failed to pack cloned git repo" if !ok

      assert { mkpathname(tarname).file? }
      mv(tarname, filepath_in_cache)
    end

    return verified?(tarname, pin, kind: :git)

  rescue LocalError => e
    error e
    return false
  ensure
    # `tmp` is assigned after the assertions above, so it is still nil
    # when one of them fires — and rm_rf(nil) then raises a TypeError
    # that REPLACES the assertion, hiding what actually went wrong.
    rm_rf(tmp) if tmp
  end # download_git_repo()

end # module Cache
