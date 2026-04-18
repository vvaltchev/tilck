# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'cache'

# SourceRef describes an upstream source artifact that a Package can
# consume: a URL, how to derive cache/remote filenames and git tags
# from a version, and whether to fetch via `git clone` vs HTTP.
#
# SourceRef is independent from Package. Multiple Packages may share
# the same SourceRef instance (e.g. gnuefi_src and gnuefi both hold
# GNUEFI_SOURCE — the tarball is fetched once, consumed twice).
#
# A SourceRef is NOT installable, NOT listable by `build_toolchain -l`,
# and NOT a node in the dep graph. It is purely a fetch+extract
# primitive. User-visible behaviour belongs to Package.
#
# Typical usage:
#
#   ZLIB_SOURCE = SourceRef.new(
#     name: 'zlib',
#     url:  GITHUB + '/madler/zlib',
#   )
#
#   class ZlibPackage < Package
#     def initialize
#       super(source: ZLIB_SOURCE, ...)
#     end
#   end
#
# Simple string/format-based customization covers the common cases;
# Procs are accepted for anything more complex.
#
class SourceRef

  attr_reader :name, :url

  # @param name            [String]    logical name; also the default
  #                                    cache filename stem (<name>-<ver>.tgz).
  # @param url             [String]    upstream URL.
  # @param tarname         [Proc(ver)] override cache filename.
  # @param remote_tarname  [Proc(ver)] override remote filename
  #                                    (defaults to the cache filename).
  # @param git_tag         [Proc(ver)] override git tag/branch
  #                                    (defaults to ver.to_s).
  # @param dir_name        [Proc(ver)] override dir name inside the
  #                                    cache tarball (defaults to ver.to_s).
  # @param fetch_via_git   [Boolean]   explicit override of the
  #                                    git-vs-HTTP auto-detection.
  def initialize(name:, url:,
                 tarname: nil,
                 remote_tarname: nil,
                 git_tag: nil,
                 dir_name: nil,
                 fetch_via_git: nil)
    @name = name
    @url = url
    @tarname_proc = tarname
    @remote_tarname_proc = remote_tarname
    @git_tag_proc = git_tag
    @dir_name_proc = dir_name
    @fetch_via_git_explicit = fetch_via_git
  end

  def tarname(ver)
    @tarname_proc ? @tarname_proc.call(ver) : "#{@name}-#{ver}.tgz"
  end

  def remote_tarname(ver)
    @remote_tarname_proc ? @remote_tarname_proc.call(ver) : tarname(ver)
  end

  def git_tag(ver)
    @git_tag_proc ? @git_tag_proc.call(ver) : ver.to_s
  end

  def dir_name(ver)
    @dir_name_proc ? @dir_name_proc.call(ver) : ver.to_s
  end

  # Whether to fetch via `git clone` (true) vs HTTP download (false).
  # Auto-detect heuristic: github.com URLs pointing at a repo root
  # — i.e. NOT at a release asset (/releases/download/) and NOT at a
  # tag archive (/archive/). Non-github git servers (git.musl-libc.org,
  # repo.or.cz) must pass fetch_via_git: true explicitly.
  def fetch_via_git?
    return @fetch_via_git_explicit if !@fetch_via_git_explicit.nil?
    tarball = @url.include?("/releases/download/") ||
              @url.include?("/archive/")
    return @url.include?(GITHUB) && !tarball
  end

  # Fetch into the cache if not already there. Idempotent — skips
  # the fetch when the cache entry exists.
  def download(ver)
    if fetch_via_git?
      Cache::download_git_repo(
        @url, tarname(ver), git_tag(ver), dir_name(ver)
      )
    else
      Cache::download_file(@url, remote_tarname(ver), tarname(ver))
    end
  end

  # Extract the cached tarball into the current working directory,
  # renaming the tarball's top-level dir to `dest_name`. The caller
  # is responsible for setting up the CWD before calling this.
  def extract(ver, dest_name)
    Cache::extract_file(tarname(ver), dest_name)
  end
end
