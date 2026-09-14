# SPDX-License-Identifier: BSD-2-Clause
#
# THE EXECUTOR: the one thing that changes the tree.
#
# It runs a Plan (plan.rb), action by action, in order: removals,
# re-marks, builds. Everything imperative about installing lives here
# -- the build itself, the records written beside it, the sysroot
# recomposed after it, the portability audit -- and nothing here
# decides anything: what to build and where was settled by the
# planner, and a Build carries every version it needs. A dry run is a
# plan that is printed and never handed to this.
#
# Each build is made by the package BOUND to the plan's scope, to the
# world as it stands when the build starts (the previous build may
# have installed what this one links), and to the versions the
# request bound. Nothing it reads comes from a block open somewhere
# up the stack.
#
# TRANSITION (docs/plans/pkgmgr-functional-core.md): the plan's scope
# is also opened around the run, so that a reader not yet converted
# to bound packages sees the right stack; the counted fallbacks say
# which those are, and the block goes when they are gone.
#

require_relative 'plan'
# InstallOrigin and InstallDeps are package.rb's, loaded by the time
# anything here runs.

module Executor

  # A build that raised rather than returned false: what it said,
  # and which package it was.
  class Failed < StandardError
    attr_reader :name
    def initialize(name, message)
      @name = name
      super(message)
    end
  end

  module_function

  # Run every action. Returns nil when all went through, else the name
  # of the package whose build failed.
  def run(registry, plan)
    return pkgmgr.with_scope(plan.scope) {
      failed = nil
      removed = false
      for a in plan.actions do
        # The sysroot is a view over what is installed, so a removal
        # invalidates it exactly as an install does -- every stack,
        # since a stale symlink is the failure mode hardest to
        # notice. Recomposed once the removals are done, before
        # anything is built against it.
        if removed && !a.is_a?(Remove)
          recompose_all
          removed = false
        end
        ok = begin
          case a
          when Build   then build(registry, a)
          when Replace then replace(registry, a)
          when Remove  then removed = true; remove(a)
          when Mark    then mark(a)
          else raise "unknown action #{a.inspect}"
          end
        rescue RuntimeError => e
          name = a.is_a?(Build) ? a.name : a.install.pkgname
          raise Failed.new("#{name}:#{a.respond_to?(:ver) ? a.ver :
                                        a.install.ver}", e.message)
        end
        if !ok
          failed = a.is_a?(Build) ? a.name : a.install.pkgname
          break
        end
      end
      recompose_all if removed
      failed
    }
  end

  def recompose_all
    pkgmgr.refresh
    pkgmgr.host_stacks.each { |v| pkgmgr.compose_stack_sysroot(Ver(v)) }
  end

  # --- the actions ----------------------------------------------------------

  # An install rebuilt where it is. The old tree is set aside first
  # -- under staging, where nothing looks for installs -- so that the
  # build sees no install and the move finds no directory; then the
  # new tree is built and moved in; and if the build does not finish
  # -- returns false OR raises, as a recipe does on a dependency it
  # cannot find -- the old tree comes back. A rebuild that removed
  # first and built second left holes exactly where the build failed.
  def replace(registry, action)

    inst = action.install
    aside = TC_STAGING / "replaced" / inst.pkg.pkg_dirname /
            File.basename(inst.path)
    FileUtils.rm_rf(aside)
    FileUtils.mkdir_p(aside.dirname)
    FileUtils.mv(inst.path, aside)
    pkgmgr.installs_changed!

    ok = false
    begin
      ok = build(registry, action.build)
    ensure
      if ok
        FileUtils.rm_rf(aside)
      else
        FileUtils.mv(aside, inst.path)
        pkgmgr.installs_changed!
      end

      # Nothing of this stays under staging: the package's directory,
      # then replaced/ itself -- each once empty, because a tree
      # stranded there by an interrupted run is not ours to take.
      [aside.dirname, aside.dirname.dirname].each { |d|
        FileUtils.rmdir(d) if Dir.empty?(d)
      }
    end

    return ok
  end

  # One installation gone, and the empty parents it leaves (the
  # package's directory, the arch's) with it, so that stale empty
  # trees do not confuse the listing.
  def remove(action)
    path = action.install.path
    FileUtils.rm_rf(path)

    parent = path.parent
    # mutation: equivalent -- the root holds cache/, never empty
    while parent != TC && parent.directory? && Dir.empty?(parent)
      FileUtils.rmdir(parent)
      parent = parent.parent
    end

    pkgmgr.installs_changed!
    return true
  end

  # Silent: what a mark is FOR is the caller's to say (a claim, a
  # --mark), and it says it.
  def mark(action)
    i = action.install
    InstallOrigin.write(i.path, i.default_install, action.manual)
    pkgmgr.installs_changed!
    return true
  end

  # One build, and everything an install carries beside what was
  # built: how the version was chosen and why it is here
  # (.install_origin), what it was built against (.install_deps), what
  # it was built from (.build_inputs, every arch the install writes),
  # the sysroot recomposed when the package contributes to it, and
  # the portability audit for a :stack package.
  def build(registry, action)

    ver = action.ver
    pkg = registry.get(action.name).at(action.scope, world: pkgmgr.world,
                                       versions: action.bound)

    ok = pkg.install_impl(ver)
    return false if ok == false
    return true if ok.nil?          # already installed: nothing to record

    # Read the world again: this build changed it.
    pkg = pkg.at(action.scope, world: pkgmgr.world, versions: action.bound)
    inst = pkg.find_install(ver)

    if inst
      InstallOrigin.write(inst.path, action.origin == :default,
                          action.mark == :manual)
      InstallDeps.write(inst.path, action.against)
      pkgmgr.installs_changed!
    end

    # Every install of this version, not just the first: gnuefi
    # builds for i386, x86_64 AND noarch from one call, and recording
    # only what find_install happened to return left two thirds of it
    # unverifiable.
    for a in pkg.install_archs(ver)
      sc = a ? action.scope.with(arch: a) : action.scope
      i = pkg.at(sc, world: pkgmgr.world, versions: action.bound)
             .find_install(ver)
      pkg.at(sc, versions: action.bound).write_build_inputs(i) if i
    end

    # The sysroot is a view over what is installed, so it is stale the
    # moment that changes. Recomposed whenever the package contributes
    # to it -- host_gcc is :distro and still contributes its target
    # runtime -- for the stack this install belongs to, which for
    # host_gcc is its own version rather than the current default.
    stack = pkg.stack_gcc_ver(ver)

    if !pkg.sysroot_fragments(stack).empty?
      pkgmgr.compose_stack_sysroot(stack)
      # Now that the sysroot includes this package, let it check
      # whatever it could not check before.
      ok = false if !pkg.post_sysroot_check(stack)
    end

    # Audited on being portable, not on contributing to the sysroot:
    # an application is exactly the thing whose linkage matters most.
    # After any composition, since a package whose paths name the
    # sysroot cannot be inspected until the sysroot is real.
    if pkg.host_tier == :stack
      ok = false if !pkgmgr.audit_portability(pkg, ver)
    end

    info "Installed package #{pkg.name} at version #{ver}"
    return ok != false
  end
end
