# SPDX-License-Identifier: BSD-2-Clause
#
# THE BRIDGE: the live package manager, read as the model's terms.
#
# The model speaks in shapes, keys and an invocation; the package
# manager speaks in Package objects, InstallInfos and constants. This
# is the translation, and it goes ONE way -- from the implementation
# to the model -- so that a comparison is between what the tree holds
# and what the contract says it should, never between two views the
# implementation produced itself.
#
# Read-only. It refreshes the package manager's scan and asks each
# package about each of its installs; it changes nothing.
#

require 'set'
require_relative 'model'

module Bridge

  module_function

  # --- registry -------------------------------------------------------------

  def registry
    return Model::Registry.new(pkgmgr.all_packages.map { |p| shape_of(p) })
  end

  def shape_of(pkg)

    # A cross compiler is the one host package whose installs carry a
    # target arch. GccPackage keeps it in @target_arch; the fake keeps
    # it in @fake_target_arch. Both are ivars because neither is part
    # of the package's public contract -- only its installs say it.
    ta = pkg.instance_variable_get(:@fake_target_arch) ||
         pkg.instance_variable_get(:@target_arch)

    kind = if !pkg.on_host
      pkg.arch_list.nil? ? :noarch : :target
    elsif ta || pkg.is_compiler
      # Both ways of being one count, as they do for -u ALL: the
      # target metadata GCC's installs carry, or the declaration alone.
      :cross_cc
    elsif pkg.name == "host_gcc"
      :stack_cc
    else
      pkg.host_tier
    end

    ia = pkg.install_archs(pkg.default_ver)
    ia = nil if ia == [nil]

    return Model::Shape.new(
      name: pkg.name,
      kind: kind,
      versions: pkg.installable_versions,
      default_ver: pkg.default_ver,
      deps: pkg.dep_list.map { |d| [d.name, d.ver] },
      arch_list: arch_names(pkg.arch_list),
      board_list: pkg.board_list,
      default: pkg.marked_default?,
      install_archs: ia&.map(&:name),
      target_arch: ta&.name,
      host_os: pkg.host_os_list,
      host_arch: pkg.host_arch_list,
      world_root: pkg.host_world_root?,
    )
  end

  # arch_list is an Array of Architectures for most packages and a
  # Hash of name => Architecture for a few; the model wants names.
  def arch_names(list)
    return nil if list.nil?
    return list.keys if list.is_a?(Hash)
    return list.map { |a| a.is_a?(Architecture) ? a.name : a.to_s }
  end

  # --- world ----------------------------------------------------------------

  # Every installation on disk, as keys. Broken installs (expected
  # files missing) are left out: the model has no such state, and the
  # implementation hides them from find_install for the same reason.
  # `fresh` reads the tree again whatever the lists remember of it.
  def world(fresh: true)

    if fresh
      pkgmgr.installs_changed!
      pkgmgr.refresh
    end

    keys = pkgmgr.all_packages.flat_map { |p|
      p.get_install_list
       .select { |i| !i.path.nil? && !i.broken }
       .map { |i| key_of(p, i) }
    }

    keys += pkgmgr.orphan_installs.map { |i| key_of(nil, i) }
    return keys.to_set
  end

  def key_of(pkg, inst)

    record = if pkg
      { ok: :ok, changed: :changed, unknown: :missing }
        .fetch(pkg.build_inputs_state_of(inst))
    else
      BuildInputs.comparable(inst.path).nil? ? :missing : :ok
    end

    return Model::Key.new(
      name: inst.pkgname,
      ver: inst.ver,
      coords: inst.coords,
      record: record,
      origin: inst.default_install ? :default : :pinned,
      mark: inst.manual ? :manual : :auto,
    )
  end

  # --- invocation -----------------------------------------------------------

  # The stack is the one in effect -- a with_host_stack around the
  # run, or an earlier -H -- which is what the implementation reads.
  def inv
    return Model::Inv.new(env_arch: ARCH, env_board: BOARD,
                          default_stack: pkgmgr.current_host_stack,
                          host_os: HOST_OS, host_arch: HOST_ARCH.name)
  end

  # Every installation that is not where its package says an install
  # of that version goes, judged at the installation's own scope. The
  # implementation asked about itself; the laws only report it.
  def misplaced

    out = []

    for p in pkgmgr.all_packages do
      for i in p.get_install_list do
        next if i.path.nil? || i.broken
        want = p.with_install_context(i) { p.coords(i.ver) }
        next if want == i.coords
        out << "#{p.name}@#{i.ver} is at #{i.coords}, its package says #{want}"
      end
    end

    return out
  end

  # Everything the laws need, at one instant. `stale` is what the
  # package manager's install lists held that the disk does not say,
  # and the reverse: the trace of a writer that moved, removed or
  # rewrote an installation and did not say installs_changed!.
  Snapshot = Struct.new(:registry, :world, :inv, :misplaced, :stale,
                        keyword_init: true)

  def snapshot
    held = world(fresh: false)
    fresh = world
    stale = if held == fresh then []
            else ["the install lists disagree with the disk:\n" \
                  "  held, not on disk: #{(held - fresh).map(&:to_s).sort}\n" \
                  "  on disk, not held: #{(fresh - held).map(&:to_s).sort}"]
            end
    return Snapshot.new(registry: registry, world: fresh, inv: inv,
                        misplaced: misplaced, stale: stale)
  end
end
