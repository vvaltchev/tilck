# SPDX-License-Identifier: BSD-2-Clause
#
# THE MUTATION DRIVER: which files, which methods, and how a mutant is
# run and judged.
#
# Scope is the logic core -- placement, identity, scoping, resolution,
# staleness -- whole files where the file is that, and named methods
# where a file mixes the core with everything else. A mutant outside
# the scope is not a question this asks.
#
# A mutant runs in its own git worktree: the file is rewritten there,
# the suite runs there against a fake toolchain, and the worktree is
# restored afterwards. Worktrees are cheap, isolate a crash, and let
# several mutants run at once. The bootstrap Ruby comes from the main
# tree, since a worktree has no toolchain of its own.
#
# Judgement: killed if the suite fails or times out, survived if it
# passes. A survivor is a missing test, and the report names the
# line.
#

require 'set'
require 'tmpdir'
require 'fileutils'
require 'timeout'
require_relative 'operators'

module Mutation

  PKGMGR = File.expand_path("../..", __dir__)      # scripts/pkgmgr
  MAIN_DIR = File.expand_path("../..", PKGMGR)      # the repository

  # Whole files in scope.
  FILES = %w[coords.rb install_selector.rb dep_resolver.rb
             version_solver.rb build_inputs.rb layout.rb].freeze

  # Files where only these methods are in scope.
  METHODS = {
    "package.rb" => %w[
      coords target_board board_bsp board_supported? arch_supported?
      with_install_context build_inputs_state_of build_inputs_changed?
      find_install installed? install_prefix install_dir pkg_dir_at
      needs_upgrade? syscc_package_get_install_list
      regular_target_package_get_install_list
      noarch_package_get_install_list all_stack_coords stack_gcc_ver
      default_cc install_archs
    ],
    "package_manager.rb" => %w[
      target_arch board_for with_target_arch with_target_coords
      with_host_stack current_host_stack stack_coords uninstall
      uninstall_selector uninstall_where force_remove
      resolve_install_plan install resolved_versions_for
      host_world_names get_stale_packages get_upgradable_packages
      build_dep_graph clean get_installed_compilers
    ],
    "main.rb" => %w[expand_install_all select_host_stack requested_arch],
  }.freeze

  Mutant = Struct.new(:site, :file) do
    def id = site.id
  end

  Verdict = Struct.new(:mutant, :status, :seconds, :tail)
  # status: :killed | :survived | :timeout | :error

  module_function

  # --- sites ----------------------------------------------------------------

  def all_mutants

    out = []

    for f in FILES do
      path = File.join(PKGMGR, f)
      src = File.binread(path)
      skip = equivalent_lines(src)
      sites(path, src).each { |s|
        out << Mutant.new(s, path) if !skip.key?(s.line)
      }
    end

    for f, names in METHODS do
      path = File.join(PKGMGR, f)
      src = File.binread(path)
      skip = equivalent_lines(src)
      within = method_ranges(src, names)
      sites(path, src, within: within).each { |s|
        out << Mutant.new(s, path) if !skip.key?(s.line)
      }
    end

    return out
  end

  # Every `# mutation: equivalent` annotation must still sit on a
  # line that has a site; otherwise it hides nothing and lies.
  def check_annotations

    bad = []

    for f in FILES + METHODS.keys do
      path = File.join(PKGMGR, f)
      src = File.binread(path)
      lines_with_sites = sites(path, src).map(&:line).to_set
      equivalent_lines(src).each { |line, reason|
        next if lines_with_sites.include?(line)
        bad << "#{f}:#{line}: annotated equivalent (#{reason}) but no " \
               "mutation site is on that line"
      }
    end

    return bad
  end

  # --- worktrees ------------------------------------------------------------

  class Worker

    attr_reader :dir

    # A worktree is HEAD; the subject is the WORKING tree, uncommitted
    # edits included, so the parts the suite reads are copied over.
    SYNCED = %w[scripts/pkgmgr other].freeze

    def initialize(index, ruby:)
      @ruby = ruby
      @dir = File.join(Dir.tmpdir, "pkgmgr-mutant-#{Process.pid}-#{index}")
      system("git", "-C", MAIN_DIR, "worktree", "add", "--detach", "-q",
             @dir, "HEAD", exception: true)
      for sub in SYNCED do
        FileUtils.rm_rf(File.join(@dir, sub))
        FileUtils.cp_r(File.join(MAIN_DIR, sub), File.join(@dir, sub))
      end
    end

    def path_of(file) = File.join(@dir, file.delete_prefix(MAIN_DIR + "/"))

    def run(mutant, timeout:)

      target = path_of(mutant.file)
      original = File.binread(target)
      File.binwrite(target, Mutation.apply(original, mutant.site))
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)

      status, tail = run_suite(timeout)

      dt = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
      return Verdict.new(mutant, status, dt, tail)
    ensure
      File.binwrite(target, original) if original
    end

    def run_suite(timeout)

      runner = File.join(@dir, "scripts", "pkgmgr", "tests", "run_all.rb")
      out = File.join(@dir, "mutant.log")
      pid = Process.spawn(
        { "MUTATION_RUN" => "1" },
        @ruby, runner, "--seed", "1",
        chdir: @dir, out: out, err: out
      )

      begin
        Timeout.timeout(timeout) { Process.wait(pid) }
      rescue Timeout::Error
        Process.kill("KILL", pid) rescue nil
        Process.wait(pid) rescue nil
        return [:timeout, ""]
      end

      text = File.read(out).lines.last(12).join
      return [$?.success? ? :survived : :killed, text]
    end

    def remove
      system("git", "-C", MAIN_DIR, "worktree", "remove", "--force", @dir)
    end
  end

  # --- the run --------------------------------------------------------------

  # Run `mutants` over `jobs` workers; yields each verdict as it lands.
  def run(mutants, jobs:, ruby:, timeout:, &on_verdict)

    workers = (0...jobs).map { |i| Worker.new(i, ruby: ruby) }
    queue = mutants.dup
    threads = workers.map { |w|
      Thread.new {
        while (m = queue.shift)
          on_verdict.call(w.run(m, timeout: timeout))
        end
      }
    }
    threads.each(&:join)
  ensure
    workers&.each(&:remove)
  end
end
