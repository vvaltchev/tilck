# SPDX-License-Identifier: BSD-2-Clause

#
# Talking to the HOST's own package manager.
#
# This is about the packages we do NOT build: the compiler, make, and
# the handful of -dev libraries a build expects to find already on the
# machine. It knows how to ask five package managers what is installed
# and how to install more, and nothing at all about WHICH packages we
# want -- that is declared per package and lives in system_deps.rb.
#
# scripts/bash_includes/install_pkgs does the same job for the
# bootstrap, where no Ruby exists yet. That list has to stay small and
# unconditional: it runs before anybody knows what is going to be
# built, so everything on it is installed on every machine whether or
# not it is ever used. This module is what lets the rest grow without
# growing that list -- a requirement declared on a package is checked
# only when that package is actually in an install plan.
#
# Deliberately free of Package and PackageManager: a future caller
# that has nothing to do with the package graph (a test runner
# checking for its own tools, say) can use it as-is.
#

require 'shellwords'

require_relative 'early_logic'

module SystemPkgs

  #
  # A host package manager: how to ask whether something is installed,
  # and how to install more.
  #
  # `installed?` is the only query. Version constraints on system
  # packages are not portably expressible across five package managers
  # with five different versioning schemes, and a requirement that
  # needs a minimum version is far better checked by running the thing
  # and reading its --version (see SysDep#command in system_deps.rb).
  #
  class Backend

    attr_reader :id, :tool

    # id:   :apt, :dnf, ... -- also the key packages are named under
    # tool: the command whose presence means this backend is usable
    def initialize(id, tool)
      @id = id
      @tool = tool
    end

    def available? = !SystemPkgs.which(@tool).nil?
    def name = @id.to_s

    def installed?(pkg) = SystemPkgs.run_quiet(query_argv(pkg))

    # Does installing need to happen as root? Homebrew says no, and
    # running it as root is an error there, not merely unnecessary.
    def needs_root? = true

    def query_argv(pkg) = raise(NotImplementedError)
    def install_argv(pkgs, assume_yes: false) = raise(NotImplementedError)

    # The full command line to install `pkgs`, privilege escalation
    # included. Kept separate from install_argv so that the escalation
    # logic is written once and so that the caller can PRINT the exact
    # command when it decides not to run it.
    def full_install_argv(pkgs, assume_yes: false)
      argv = install_argv(pkgs, assume_yes: assume_yes)
      return needs_root? ? SystemPkgs.as_root(argv) : argv
    end
  end

  # Debian, Ubuntu, Mint and friends.
  class AptBackend < Backend

    def initialize = super(:apt, "dpkg-query")

    # Ask for the status field rather than trusting `dpkg -s`'s exit
    # code or grepping for the word "Status". A package that was
    # removed but not purged still has a Status line -- "deinstall ok
    # config-files" -- and still exits 0, so both of the obvious
    # checks call it installed when none of its files are there.
    def installed?(pkg)
      ok, out = SystemPkgs.run_capture(
        ["dpkg-query", "-W", "-f=${db:Status-Status}", pkg]
      )
      return ok && out.strip == "installed"
    end

    def install_argv(pkgs, assume_yes: false)
      return ["apt", "install", *(assume_yes ? ["-y"] : []), *pkgs]
    end
  end

  # Fedora, RHEL and rebuilds.
  class DnfBackend < Backend

    def initialize = super(:dnf, "rpm")
    def query_argv(pkg) = ["rpm", "-q", pkg]

    def install_argv(pkgs, assume_yes: false)
      return ["dnf", "install", *(assume_yes ? ["-y"] : []), *pkgs]
    end
  end

  # Arch, Manjaro, Artix.
  class PacmanBackend < Backend

    def initialize = super(:pacman, "pacman")
    def query_argv(pkg) = ["pacman", "-Q", pkg]

    def install_argv(pkgs, assume_yes: false)
      extra = assume_yes ? ["--noconfirm"] : []
      return ["pacman", "-S", "--needed", *extra, *pkgs]
    end
  end

  # FreeBSD.
  class PkgBackend < Backend

    def initialize = super(:pkg, "pkg")
    def query_argv(pkg) = ["pkg", "info", "-e", pkg]

    def install_argv(pkgs, assume_yes: false)
      return ["pkg", "install", *(assume_yes ? ["-y"] : []), *pkgs]
    end
  end

  # macOS. Homebrew installs into a prefix the user owns.
  class BrewBackend < Backend

    def initialize = super(:brew, "brew")
    def query_argv(pkg) = ["brew", "list", "--formula", pkg]
    def needs_root? = false

    def install_argv(pkgs, assume_yes: false)
      return ["brew", "install", *pkgs]
    end
  end

  #
  # Which package manager this machine uses.
  #
  # ID_LIKE from /etc/os-release is what makes this work for
  # derivatives nobody enumerated: Mint says ID=linuxmint
  # ID_LIKE=ubuntu, Rocky says ID_LIKE="rhel centos fedora". The lists
  # below therefore only have to name the distros that are their own
  # family root, plus the few whose ID_LIKE is missing or unhelpful.
  #
  FAMILY_IDS = {
    apt:    %w[debian ubuntu linuxmint raspbian devuan pop],
    dnf:    %w[fedora rhel centos rocky almalinux ol],
    pacman: %w[arch archlinux manjaro artix endeavouros],
  }.freeze

  BACKENDS = {
    apt:    -> { AptBackend.new },
    dnf:    -> { DnfBackend.new },
    pacman: -> { PacmanBackend.new },
    pkg:    -> { PkgBackend.new },
    brew:   -> { BrewBackend.new },
  }.freeze

  module_function

  # The backend for this host, or nil when the distro is one we have
  # no support for. nil is a normal outcome, not an error: a machine
  # we cannot install on can still be told what is missing.
  def detect(host_os = HOST_OS, ids: nil)
    case host_os
      when "macos"   then return BACKENDS[:brew].call
      when "freebsd" then return BACKENDS[:pkg].call
      when "linux"   then return detect_linux(ids || distro_ids)
    end
    return nil
  end

  def detect_linux(ids)
    FAMILY_IDS.each do |family, names|
      return BACKENDS[family].call if ids.any? { |i| names.include?(i) }
    end

    # An unlisted distro can still be served if one of the tools is
    # there. Better than giving up: the ID lists above will always be
    # behind the world.
    [:apt, :dnf, :pacman].each do |family|
      b = BACKENDS[family].call
      return b if b.available?
    end

    return nil
  end

  # ID plus ID_LIKE, lowercased, ID first.
  def distro_ids
    data = InitOnly.parse_os_release()
    ids = [data["ID"].to_s.downcase]
    ids += data["ID_LIKE"].to_s.downcase.split(/\s+/)
    return ids.reject(&:empty?)
  end

  def backend
    @backend = detect() if !defined?(@backend) || @backend.nil?
    return @backend
  end

  # Locate an executable. `extra_dirs` covers the installers that do
  # not go through the system package manager and do not always end up
  # on PATH -- rustup's ~/.cargo/bin above all.
  def which(cmd, extra_dirs: [])
    if cmd.include?(File::SEPARATOR)
      return File.executable?(cmd) && !File.directory?(cmd) ? cmd : nil
    end

    dirs = ENV["PATH"].to_s.split(File::PATH_SEPARATOR) + extra_dirs

    for d in dirs do
      next if d.to_s.empty?
      p = File.join(d, cmd)
      return p if File.executable?(p) && !File.directory?(p)
    end

    return nil
  end

  def run_quiet(argv)
    return !!system(*argv.map(&:to_s), out: File::NULL, err: File::NULL)
  rescue SystemCallError
    return false
  end

  # [ok, stdout]. Used for the small outputs of `--version` and
  # `dpkg-query -W`; popen rather than a pipe we read after waiting,
  # so a chatty command cannot deadlock us by filling the buffer.
  def run_capture(argv)
    out = IO.popen([*argv.map(&:to_s), err: File::NULL], &:read)
    return [$?.success?, out.to_s]
  rescue SystemCallError, IOError
    return [false, ""]
  end

  # Wrap a command so it runs as root.
  #
  # `su -c` takes one string, so the argv has to be flattened and
  # escaped for it; sudo does not, and is left as a real argv.
  def as_root(argv)
    return argv if Process.uid == 0
    return ["sudo", *argv] if which("sudo")
    return ["su", "-c", argv.map { |a| Shellwords.escape(a.to_s) }.join(" ")]
  end

  # Can we ask the user a question and expect an answer?
  def interactive? = STDIN.tty? && STDOUT.tty?

  def in_ci?
    return !ENV["RUNNING_IN_CI"].to_s.strip.empty? ||
           !ENV["CI"].to_s.strip.empty?
  end

  # Render a command line the way a human would type it, so that a
  # message telling somebody to run it can be copied verbatim.
  def cmd_to_s(argv) = argv.map { |a| Shellwords.escape(a.to_s) }.join(" ")

end
