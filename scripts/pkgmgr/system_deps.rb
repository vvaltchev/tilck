# SPDX-License-Identifier: BSD-2-Clause

require 'tmpdir'

require_relative 'early_logic'
require_relative 'term'
require_relative 'version'
require_relative 'system_pkgs'

#
# What a package needs from the HOST, as opposed to what it needs from
# us.
#
# A package declares its system-level requirements by overriding
# Package#system_deps, and the install path checks the union of those
# declarations across the whole transitive closure BEFORE building
# anything. The point is when the failure happens: a missing Rust
# toolchain should stop the run in the first second, with a sentence
# saying what to install, not forty minutes in when a configure script
# finally looks for it.
#
# Two shapes of requirement, and the difference matters:
#
#   * a PACKAGE -- "libssl-dev" -- which only the host package manager
#     can answer for, under a different name per distro;
#
#   * a COMMAND -- "rustc" -- which is checked by running it. A
#     command can arrive from places the package manager has never
#     heard of: rustup, Homebrew, a hand-built tree on PATH. Asking
#     dpkg about rustc on a machine with a working rustup toolchain
#     gets you "not installed", and installing what it then offers
#     makes things worse rather than better. So a command is looked
#     for on PATH plus the handful of prefixes those installers use,
#     and its --version is parsed and compared, exactly the way
#     version_check.rb vets the Ruby it is running under.
#
# The two are not alternatives to each other; they are the two kinds
# of thing that can be missing.
#

module SystemDeps

  # An external toolchain installer -- something that is not the
  # system package manager but is the RIGHT way to get a particular
  # dependency. It downloads and runs code from the network, so it is
  # never invoked without consent.
  class Installer

    attr_reader :id, :what, :url, :bin_dir, :why

    def initialize(id:, what:, url:, bin_dir: nil, why: nil)
      @id = id
      @what = what
      @url = url
      @bin_dir = bin_dir
      @why = why
    end

    # Subclasses do the actual work. Returns true on success.
    def run(env) = raise(NotImplementedError)
  end

  #
  # rustup, the official Rust toolchain installer.
  #
  # Rust is the dependency that motivates having this at all. The
  # distro package is usually behind, and on the older distros it is
  # behind in a way that installing it does not fix: Ubuntu 22.04 --
  # the machine this was written on -- ships rustc 1.66, while glycin
  # 2.0 declares rust-version = "1.85". Installing apt's rustc there
  # does not solve the problem, it just moves the failure from the
  # start of the run into the middle of a cargo build.
  #
  # rustup is what upstream Rust supports, it needs no root, and it
  # installs a current toolchain on every distro equally.
  #
  class RustupInstaller < Installer

    RUSTUP_URL = "https://sh.rustup.rs"

    def initialize
      super(
        id: :rustup,
        what: "rustup (the official Rust toolchain installer)",
        url: RUSTUP_URL,
        bin_dir: File.join(Dir.home, ".cargo", "bin"),
        why: "the distro's rustc is often too old to build modern " \
             "crates, and on some distros cannot be updated at all",
      )
    end

    def run(env)
      script = File.join(Dir.tmpdir, "tilck-rustup-init.sh")

      # Downloaded to a file and then run, rather than piped straight
      # into a shell: the script stays on disk afterwards, so what was
      # executed can be read.
      info "Downloading #{RUSTUP_URL} to #{script}"

      ok = env.run([
        "curl", "--proto", "=https", "--tlsv1.2", "-sSf",
        RUSTUP_URL, "-o", script,
      ])

      if !ok
        error "Could not download the rustup installer"
        return false
      end

      # Whether rustup may put cargo and rustc on PATH for good.
      #
      # This build does not need it -- we prepend bin_dir to our own
      # PATH below either way -- but somebody installing Rust probably
      # wants to use it outside this build too, so it is worth asking
      # rather than deciding for them in either direction. Editing a
      # shell profile is not something to do to an unattended machine,
      # so a run with nobody to ask does not.
      modify_path = env.interactive? &&
                    env.ask("Add #{bin_dir} to your PATH as well " \
                            "(updates your shell profile)?")

      argv = ["sh", script, "-y"]
      argv << "--no-modify-path" if !modify_path
      ok = env.run(argv)

      if !ok
        error "rustup installation failed"
        return false
      end

      # Ours regardless: the profile edit, if any, only affects future
      # shells, and this build needs the toolchain now.
      prepend_to_global_path(Pathname.new(bin_dir)) if File.directory?(bin_dir)

      info "Rust installed in #{bin_dir}"

      if modify_path
        info "Your shell profile was updated; new shells will find it."
      else
        info "To use it outside this build:"
        info "  export PATH=\"#{bin_dir}:$PATH\""
      end

      return true
    end
  end

  RUSTUP = RustupInstaller.new

  #
  # `cargo install <crate>`.
  #
  # Rust's build tools are distributed as crates rather than as distro
  # packages, so the thing that installs them is cargo. No distro
  # names cargo-c, and building it ourselves would mean vendoring a
  # crate tree to produce a tool that only runs at build time -- so it
  # is a host dependency like rustc, installed the way upstream
  # installs it.
  #
  # Needs no root: cargo installs into ~/.cargo/bin, which is already
  # searched (see extra_bin_dirs).
  #
  class CargoInstaller < Installer

    attr_reader :crate

    def initialize(crate:, what:, why: nil)
      @crate = crate
      super(id: :"cargo_install_#{crate}",
            what: what,
            url: "https://crates.io/crates/#{crate}",
            bin_dir: File.join(Dir.home, ".cargo", "bin"),
            why: why)
    end

    def run(env)
      cargo = env.which("cargo")

      if cargo.nil?
        error "cargo is needed to install #{@crate}, and is not present"
        return false
      end

      if !env.run([cargo, "install", @crate])
        error "`cargo install #{@crate}` failed"
        return false
      end

      prepend_to_global_path(Pathname.new(bin_dir)) if File.directory?(bin_dir)
      info "#{@crate} installed in #{bin_dir}"
      return true
    end
  end

  CARGO_C_INSTALLER = CargoInstaller.new(
    crate: "cargo-c",
    what: "cargo-c (cargo install cargo-c)",
    why: "no distro packages it, and it is a build-time tool rather " \
         "than a library",
  )

  #
  # One declared system-level requirement.
  #
  # `pkgs` names the package per backend -- { apt: "libssl-dev", dnf:
  # "openssl-devel" } -- or a bare String when every distro agrees.
  # `command` makes it a command-shaped dep instead, checked by
  # running it. `min_ver` needs `command`: a version constraint is not
  # portably expressible across five package managers, but every
  # toolchain in the world prints its version.
  #
  class SysDep

    DEFAULT_VER_RE = /(\d+(?:\.\d+)+)/

    attr_reader :key, :what, :pkgs, :command, :version_flag, :version_re,
                :min_ver, :installer

    def initialize(key:, what:, pkgs: {}, command: nil,
                   version_flag: "--version", version_re: DEFAULT_VER_RE,
                   min_ver: nil, installer: nil)
      @key = key
      @what = what
      @pkgs = pkgs.is_a?(String) ? Hash.new(pkgs) : pkgs
      @command = command
      @version_flag = version_flag
      @version_re = version_re
      @min_ver = min_ver
      @installer = installer

      raise "min_ver needs a command to check it with" if min_ver && !command
    end

    # "rustc >= 1.85" / "libssl-dev"
    def label
      base = @command || @key.to_s
      return @min_ver ? "#{base} >= #{@min_ver}" : base
    end

    # The package name for a backend, or nil when this dep is not
    # available from it under any name we know.
    def pkg_for(backend_id) = @pkgs[backend_id]

    def check(env)
      return @command ? check_command(env) : check_package(env)
    end

    def check_command(env)
      path = env.which(@command)
      return Result.new(self, :missing) if !path

      ver = env.probe_version(path, @version_flag, @version_re)

      # It is on disk but would not run, or ran and printed nothing we
      # could read a version out of. Distinct from missing: a broken
      # or shadowed binary is not fixed by installing the package.
      return Result.new(self, :broken, path) if ver.nil?

      state = (@min_ver.nil? || ver >= @min_ver) ? :ok : :too_old
      return Result.new(self, state, path, ver)
    end

    def check_package(env)
      b = env.backend
      name = b ? pkg_for(b.id) : nil

      # Say so rather than guessing. An unsupported distro, or a
      # package we have no name for there, is a thing we do not know
      # -- reporting it as satisfied would be a lie and reporting it
      # as missing would send people installing something that may
      # well already be there under another name.
      return Result.new(self, :unknown) if !name

      return Result.new(self, b.installed?(name) ? :ok : :missing)
    end
  end

  # The outcome of checking one SysDep.
  class Result

    attr_reader :dep, :state, :path, :ver

    def initialize(dep, state, path = nil, ver = nil)
      @dep = dep
      @state = state
      @path = path
      @ver = ver
    end

    def ok? = @state == :ok

    # Whether we know enough to say it is a problem. :unknown is not
    # satisfied, but it is not grounds for refusing to build either.
    def blocking? = [:missing, :too_old, :broken].include?(@state)

    def detail
      case @state
        when :ok       then @ver ? "found #{@path} (#{@ver})" : "installed"
        when :missing  then "not found"
        when :too_old  then "found #{@path} but it is #{@ver}"
        when :broken   then "#{@path} exists but would not report a version"
        when :unknown  then "cannot check on this distro"
      end
    end
  end

  #
  # The requirements themselves.
  #

  # Fixed by what we actually build, not by the newest Rust there is.
  #
  # gdk-pixbuf 2.44.8's meson.build asks for `glycin-2 >= 2.2.alpha.7`,
  # and glycin 2.2.alpha.7's Cargo.toml declares rust-version = "1.93".
  # Older glycin would do with less (2.0 wanted 1.85) but gdk-pixbuf
  # will not accept it. Raise this only when something in the closure
  # actually demands it: every bump here is a machine that stops
  # being able to build Tilck's toolchain.
  MIN_RUST = Ver("1.93")

  RUSTC = SysDep.new(key: :rustc, what: "the Rust compiler",
                     command: "rustc", min_ver: MIN_RUST,
                     installer: RUSTUP)

  CARGO = SysDep.new(key: :cargo, what: "Cargo, Rust's build tool",
                     command: "cargo", min_ver: MIN_RUST,
                     installer: RUSTUP)

  # Builds a C ABI -- shared library, header and .pc file -- out of a
  # Rust crate. librsvg's meson.build asks for the `cargo-cbuild`
  # binary by name and will not configure without it.
  CARGO_C = SysDep.new(key: :cargo_c,
                       what: "cargo-c, which gives a Rust crate a C ABI",
                       command: "cargo-cbuild",
                       installer: CARGO_C_INSTALLER)

  #
  # Everything that touches the world outside this process, in one
  # object, so that the decision logic above and the reporting below
  # are testable without a package manager, a network, or a terminal.
  #
  class Env

    def which(cmd)
      return SystemPkgs.which(cmd, extra_dirs: SystemDeps.extra_bin_dirs)
    end

    def backend = SystemPkgs.backend

    def probe_version(path, flag, re)
      ok, out = SystemPkgs.run_capture([path, flag])
      return nil if !ok
      m = out.match(re)
      return m ? SafeVer(m[1]) : nil
    end

    def run(argv) = !!system(*argv.map(&:to_s))
    def ask(q, default: true) = Term.ask_yes_no(q, default: default)
    def interactive? = SystemPkgs.interactive?
    def in_ci? = SystemPkgs.in_ci?
  end

  module_function

  # Toolchains that are installed but not necessarily on PATH.
  #
  # rustup puts rustc and cargo in ~/.cargo/bin, and when run with
  # --no-modify-path (which is how we run it) it leaves the user's
  # shell profile alone -- so a perfectly good toolchain is invisible
  # to a non-login shell. Homebrew's prefixes are here for the same
  # reason.
  def extra_bin_dirs
    dirs = []
    home = (Dir.home rescue nil)
    dirs << File.join(home, ".cargo", "bin") if home
    dirs += ["/opt/homebrew/bin", "/usr/local/bin"] if HOST_OS == "macos"
    return dirs.select { |d| File.directory?(d) }
  end

  # Every SysDep declared by the packages in `pairs`, deduplicated,
  # each paired with the packages that asked for it.
  #
  # Returns: Array of [SysDep, [pkg_name, ...]], in declaration order.
  def collect(pairs, pm)
    out = {}

    for name, ver in pairs do
      pkg = pm.get(name)
      next if pkg.nil?

      for dep in pkg.system_deps(ver) do
        entry = (out[dep.key] ||= [dep, []])
        entry[1] << name if !entry[1].include?(name)
      end
    end

    return out.values
  end

  #
  # The pre-flight check. `pairs` is the install plan: [name, ver].
  #
  # Returns false only when a requirement is genuinely unmet and could
  # not be resolved -- that is, when going ahead would waste the
  # user's time on a build that cannot succeed.
  #
  def check_plan(pairs, dry_run: false, env: Env.new, pm: nil)
    entries = collect(pairs, pm || pkgmgr)
    return true if entries.empty?

    results = entries.map { |dep, users| [dep.check(env), users] }
    unmet = results.reject { |r, _| r.ok? }

    if unmet.empty?
      for r, _ in results do
        info "System dependency OK: #{r.dep.label} -- #{r.detail}"
      end
      return true
    end

    report(unmet)

    # -d is a question, not an action: say what is missing and how it
    # would be fixed, then leave the machine as it was found.
    if dry_run
      info "Dry run (-d): system dependencies not installed"
      return true
    end

    return remediate(unmet, env)
  end

  def report(unmet)
    error "#{unmet.length} system " \
          "#{unmet.length == 1 ? "dependency is" : "dependencies are"} " \
          "not satisfied:"

    for r, users in unmet do
      puts "  * #{r.dep.label} (#{r.dep.what})"
      puts "      #{r.detail}"
      puts "      needed by: #{users.join(", ")}"
    end

    puts
  end

  # Try to fix what `report` just listed. Returns true when everything
  # is satisfied afterwards.
  def remediate(unmet, env)
    blocking = unmet.select { |r, _| r.blocking? }

    # Nothing actionable: everything left is :unknown, which means we
    # could not check rather than that it is missing. Warn and go on
    # -- refusing to build because we cannot verify a dependency on an
    # unsupported distro would be worse than letting the build try.
    if blocking.empty?
      warning "Could not verify the above on this distro; continuing"
      return true
    end

    by_pkgmgr, rest = split_by_remedy(blocking, env.backend)
    via_installer, stuck = rest.partition { |r, _| r.dep.installer }

    if !stuck.empty?
      error "No way to install: " \
            "#{stuck.map { |r, _| r.dep.label }.join(", ")}"
      error "Please install them manually and re-run."
      return false
    end

    return false if !by_pkgmgr.empty? && !install_pkgs(by_pkgmgr, env)
    return false if !via_installer.empty? && !run_installers(via_installer,
                                                             env)
    return recheck(blocking, env)
  end

  # Split into "the host package manager can install this" and the
  # rest. A dep whose distro package is too old is deliberately NOT in
  # the first group: reinstalling what is already there fixes nothing.
  def split_by_remedy(blocking, backend)
    return [[], blocking] if backend.nil?

    return blocking.partition { |r, _|
      r.state != :too_old && !r.dep.pkg_for(backend.id).nil?
    }
  end

  def install_pkgs(entries, env)
    backend = env.backend
    names = entries.map { |r, _| r.dep.pkg_for(backend.id) }.uniq

    # Shown without -y even when we would run it with -y: this line
    # exists to be copied and typed by a person, and a person running
    # it by hand wants to see what their package manager proposes to
    # do before agreeing to it.
    info "These can be installed with #{backend.name}:"
    info "  #{SystemPkgs.cmd_to_s(backend.full_install_argv(names))}"

    if !env.interactive? && !env.in_ci?
      error "Not running interactively: refusing to install packages."
      error "Run the command above, or re-run this in a terminal."
      return false
    end

    if env.interactive? && !env.ask("Install #{names.length} package(s) now?")
      error "Declined. The packages above are still required."
      return false
    end

    # -y only where nobody can answer the package manager's own
    # prompt: an unattended run that stops on a y/n question hangs.
    unattended = env.in_ci? || !env.interactive?
    return env.run(backend.full_install_argv(names, assume_yes: unattended))
  end

  # Run the non-distro installers (rustup) for what they cover.
  #
  # Exactly the policy the host package manager gets, and for the same
  # reason: a terminal is asked, an unattended run that has said it is
  # unattended (CI) proceeds, and a run that is neither refuses rather
  # than acting on somebody's machine with nobody watching. Splitting
  # the two -- letting CI install packages but not a toolchain -- only
  # produces CI that cannot build what it was asked to build.
  def run_installers(entries, env)
    groups = entries.group_by { |r, _| r.dep.installer }

    for installer, group in groups do
      labels = group.map { |r, _| r.dep.label }.join(", ")

      info "#{labels}: the recommended way to install this is " \
           "#{installer.what}"
      info "Reason: #{installer.why}" if installer.why
      info "It downloads and runs #{installer.url}"

      if !env.interactive? && !env.in_ci?
        error "Not running interactively: refusing to download and run " \
              "an installer."
        error "Install #{labels} yourself, or re-run this in a terminal."
        return false
      end

      if env.interactive? && !env.ask("Install #{installer.what} now?")
        error "Declined. #{labels} is still required."
        return false
      end

      return false if !installer.run(env)
    end

    return true
  end

  # Ask again, from scratch, after installing. The install command
  # exiting 0 is not evidence that the requirement is now met -- a
  # package manager will happily install a rustc that is still too
  # old.
  def recheck(blocking, env)
    still = blocking.map { |r, users| [r.dep.check(env), users] }
                    .reject { |r, _| r.ok? }

    if !still.empty?
      error "Still not satisfied after installing:"
      for r, _ in still do
        puts "  * #{r.dep.label}: #{r.detail}"
      end
      return false
    end

    info "All system dependencies are now satisfied"
    return true
  end

end
