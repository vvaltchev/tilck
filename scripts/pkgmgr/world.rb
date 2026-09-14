# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT IS INSTALLED, AS ONE VALUE.
#
# The tree is read once, into a World, and every question about what
# is installed is asked of that value: which installs a package has,
# whether one exists at some coordinates, what is on disk that no
# package claims. Before this, each package remembered its own list
# against a generation counter and the manager kept four caches of
# its own, refreshed by whoever remembered to; a question asked
# between a write and a refresh was answered from the tree before.
#
# A World is a snapshot. Writers (the executor, once there is one)
# announce a change and the next question scans again; the planner
# will take a World as an argument and hold no other view of the tree.
#
# The scan walks <machine>/<env>/<stack>/pkgs/<pkg>/<ver>/ for what
# no registered package claims -- what a rename or a removal leaves
# behind, which `-l` lists as "found" and `-u` must be able to name
# -- and takes each package's own reading of its directories for the
# rest. One loop for everything, because every install is at the
# same depth with the same meaning per level; toolchain4 needed four
# different walks and a predicate to tell a compiler directory from a
# package with a similar name.
#

require 'set'
require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'coords'
require_relative 'build_inputs'

# `tc` is the tree the installs were read from: the tests swap TC,
# and a world of another tree answers nothing about this one.
World = Data.define(:installs, :tc) do

  # Top-level directories that are not machines.
  NON_INSTALL_DIRS = ["cache", "staging"].freeze

  # The tree as `packages` see it, plus what none of them claims.
  def self.scan(packages, host: Host.env)
    claimed = packages.flat_map { |p| p.read_install_list(host) }
    known = claimed.map(&:path).to_set
    return new(installs: (claimed + orphans_of(known, host)).freeze, tc: TC)
  end

  def self.empty = new(installs: [].freeze, tc: nil)

  # The same installs, as a value with no tree behind it.
  def self.of(installs) = new(installs: installs.to_a.freeze, tc: nil)

  # Installs a registered package claims, and those none does.
  def claimed = installs.select { |i| !i.pkg.nil? }
  def orphans = installs.select { |i| i.pkg.nil? }

  # Every install of the package called `name`, broken ones included.
  def of(name) = installs.select { |i| i.pkgname == name && !i.pkg.nil? }

  # The same world, every install carrying what its record says
  # (InstallInfo#record), each judged as its package's recipe reads
  # at the install's own coordinates. An orphan has no recipe to
  # judge against: it is :ok with a record and :unknown without.
  # Asked for by the modes that need it (-l, --check-for-updates,
  # --rebuild); a build has no use for it and does not pay for it.
  def judged(registry, scope)
    return World.new(installs: installs.map { |i|
      state = if i.pkg
        i.pkg.at(scope, world: self).build_inputs_state_of(i)
      else
        BuildInputs.comparable(i.path).nil? ? :unknown : :ok
      end
      i.with_record(state)
    }.freeze, tc: tc)
  end

  # The one install of `name` at `ver` and `coords` that is complete.
  def find(name, ver, coords)
    return of(name).find { |i|
      i.ver == ver && i.coords == coords && !i.broken
    }
  end

  # --- the walk for what nobody claims -------------------------------------

  # Walk the whole toolchain and emit an InstallInfo per <pkg>/<ver>
  # directory whose path is not in `known`.
  def self.orphans_of(known, host)

    list = []
    return list if !TC.directory?

    for machine in Dir.children(TC).sort
      next if NON_INSTALL_DIRS.include?(machine)

      m_dir = TC / machine
      next if !m_dir.directory?

      arch_obj, on_host, is_known = machine_to_arch(machine, host)
      next if !is_known

      for env in Dir.children(m_dir).sort
        e_dir = m_dir / env
        next if !e_dir.directory?

        for stack in Dir.children(e_dir).sort
          pkgs = e_dir / stack / "pkgs"
          next if !pkgs.directory?

          c = Coords.new(machine, env, stack)

          for pkg_name in Dir.children(pkgs).sort
            scan_one_pkg_dir(pkgs / pkg_name, pkg_name, arch_obj, on_host,
                             c, known, list)
          end
        end
      end
    end

    return list
  end

  # Scan one <pkg>/ directory, whose children are version directories.
  #
  # A version is read only from the version level, so a package's own
  # subdirectories (bin/, share/) are never mistaken for versions --
  # which is what happened when a directory prefix alone decided what
  # was a compiler slot: the musl cross compilers are packages CALLED
  # gcc-i386-musl, and reading them as slots descended one level too
  # far.
  def self.scan_one_pkg_dir(pkg_path, pkg_name, arch_obj, on_host, coords,
                            known, list)

    return if !pkg_path.directory?

    for ver_str in Dir.children(pkg_path)
      full_path = pkg_path / ver_str
      next if known.include?(full_path)
      ver = SafeVer(ver_str)

      if ver.nil?
        warning "Invalid package version: #{full_path}"
        next
      end

      list << InstallInfo.new(
        pkg_name, "syscc", on_host, arch_obj, ver, full_path,
        coords: coords
      )
    end
  end

  # Turn a <machine> coordinate back into [Architecture, on_host].
  #
  # "noarch" has neither; a Tilck target names its arch directly; and
  # anything else is a build machine, whose packages run on the host.
  # Returns [Architecture, on_host, known?]. A machine we cannot
  # identify is skipped rather than scanned: reading its packages
  # would attribute them to a nil architecture and let them show up
  # in listings for a target that does not exist.
  def self.machine_to_arch(machine, host)

    return [nil, false, true] if machine == "noarch"

    if Coords.target_machine?(machine)
      arch = Coords.target_arch_of(machine)
      warning "Unknown architecture in #{TC / machine}" if !arch
      return [arch, false, !arch.nil?]
    end

    # Any other machine is a build host. Only this one's packages can
    # run here, so anything else is another machine's business.
    return [host.arch, true, machine == host.machine]
  end
end
