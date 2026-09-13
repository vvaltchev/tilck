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

  module_function

  # Run every action. Returns nil when all went through, else the name
  # of the package whose build failed.
  def run(registry, plan)
    return pkgmgr.with_scope(plan.scope) {
      failed = nil
      for a in plan.actions do
        ok = case a
             when Build  then build(registry, a)
             when Remove then remove(a)
             when Mark   then mark(a)
             else raise "unknown action #{a.inspect}"
             end
        if !ok
          failed = a.is_a?(Build) ? a.name : a.install.pkgname
          break
        end
      end
      failed
    }
  end

  # --- the actions ----------------------------------------------------------

  def remove(action)
    i = action.install
    pkgmgr.uninstall(i.pkgname, false, false, i.ver, coords: [i.coords])
    return true
  end

  def mark(action)
    i = action.install
    info "Set #{i.pkgname}:#{i.ver} to #{action.manual ? 'manually' :
                                          'automatically'} installed"
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
