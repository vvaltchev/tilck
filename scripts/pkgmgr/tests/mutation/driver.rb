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
# Judgement: killed if the suite fails, survived if it passes, and a
# timeout if it hangs. A survivor is a missing test; a timeout is a
# walk that nothing bounds, which is a defect in the code and not in
# the tests. The report names the line either way.
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

  # The whole certification, as both pmmutate and `-t --mutation` run
  # it. Returns true when every mutant in scope was killed.
  #
  # The instrument tests itself first: the UNMUTATED suite must pass
  # in a worktree, or nothing can be judged -- a suite that fails on
  # its own would "kill" every mutant. That run also sets the timeout:
  # a mutant is given several times what the clean suite took, so the
  # bound follows the machine rather than a number chosen elsewhere.
  def certify(mutants, jobs:, ruby:, timeout: nil, out: $stdout)

    out.puts "mutation: #{mutants.length} mutants, #{jobs} workers"
    out.puts "mutation: the suite must pass unmutated first..."

    probe = Worker.new("probe", ruby: ruby)
    begin
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      status, tail = probe.run_suite(timeout || 1800)
      clean = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
    ensure
      probe.remove
    end

    if status != :survived
      out.puts "mutation: the unmutated suite did not pass " \
               "(#{status}); refusing to judge anything:"
      out.puts tail
      return false
    end

    timeout ||= [120, (clean * 5).ceil].max
    out.puts format("mutation: clean suite %.0fs, timeout %ds per mutant",
                    clean, timeout)

    verdicts = []
    lock = Mutex.new

    run(mutants, jobs: jobs, ruby: ruby, timeout: timeout) { |v|
      lock.synchronize {
        verdicts << v
        mark = { killed: "killed  ", survived: "SURVIVED",
                 timeout: "TIMEOUT ", error: "error   " }[v.status]
        out.printf("  [%4d/%4d] %s  %5.1fs  %s\n", verdicts.length,
                   mutants.length, mark, v.seconds, v.mutant.site)
        out.flush
      }
    }

    by = verdicts.group_by(&:status).transform_values(&:length)
    survivors = verdicts.select { |v| v.status == :survived }
    hung = verdicts.select { |v| v.status == :timeout }

    out.puts
    out.puts "mutation: #{verdicts.length} mutants: #{by[:killed] || 0} " \
             "killed, #{hung.length} timed out, #{survivors.length} survived"

    if !survivors.empty?
      out.puts
      out.puts "SURVIVORS -- each is a test that does not exist:"
      survivors.sort_by { |v| [v.mutant.file, v.mutant.site.line] }
               .each { |v| out.puts "  #{v.mutant.site}" }
    end

    # A mutant that hangs is not caught; it is waited out. The suite
    # would have hung too, and a hang is the one failure nothing
    # downstream can report: the walk it broke needs a bound.
    if !hung.empty?
      out.puts
      out.puts "TIMEOUTS -- each is a walk without a bound:"
      hung.sort_by { |v| [v.mutant.file, v.mutant.site.line] }
          .each { |v| out.puts "  #{v.mutant.site}" }
    end

    return survivors.empty? && hung.empty?
  end

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
