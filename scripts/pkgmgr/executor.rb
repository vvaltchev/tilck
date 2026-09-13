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
#

require_relative 'plan'
require_relative 'stack_manifest'
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
    write_missing_manifests(registry)
    return failed
  end

  # A stack from before manifests gets one, derived the way the code
  # used to guess: from the compiler install whose version it names,
  # wherever that install is now. Once per run, after the actions,
  # so that a tree from before the record catches up the first time
  # anything is written to it and never needs telling.
  def write_missing_manifests(registry)
    scope = pkgmgr.env_scope
    for pkg in registry.all_packages do
      for inst in pkgmgr.world.of(pkg.name) do
        next if inst.broken
        bound = pkg.at(scope, world: pkgmgr.world)
        pairs = bound.stacks_defined(inst.ver, InstallDeps.read(inst.path),
                                     compiler_at: inst.coords)
        for c, m in pairs do
          next if !c.root.directory? || StackManifest.read(c)
          StackManifest.write(c, m)
        end
      end
    end

    # A host stack whose compiler the world no longer sees -- the
    # distro env moved, and the compiler is under the old one -- is
    # exactly the stack that needs its manifest most. Its compiler is
    # looked for under every env of this host, by the version the
    # stack names, and recorded where it is found.
    gcc = pkgmgr.stack_compiler
    return if gcc.nil?
    bound = gcc.at(scope, world: pkgmgr.world)
    for id in pkgmgr.host_stacks(host: scope.host) do
      stack = pkgmgr.stack_coords(id, host: scope.host)
      next if StackManifest.read(stack)
      at = compiler_anywhere(bound, id.ver, scope.host)
      next if at.nil?
      dir = bound.pkg_dir_at(at) / bound.ver_dirname(id.ver)
      for c, m in bound.stacks_defined(id.ver, InstallDeps.read(dir),
                                       compiler_at: at) do
        StackManifest.write(c, m) if !StackManifest.read(c)
      end
    end
  end

  # The coordinates under `host`'s machine, whatever the env, holding
  # a complete install of `pkg` at `ver`; nil when none does.
  def compiler_anywhere(pkg, ver, host)
    machine = TC / host.machine
    return nil if !machine.directory?
    for env in Dir.children(machine).sort do
      c = Coords.new(host.machine, env, nil)
      dir = pkg.pkg_dir_at(c) / pkg.ver_dirname(ver)
      return c if dir.directory? && pkg.check_install_dir(dir, ver)
    end
    return nil
  end

  def recompose_all
    pkgmgr.refresh
    pkgmgr.host_stacks.each { |id| pkgmgr.compose_stack_sysroot(id) }
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

    # The tree changed, whoever wrote it: the base install_impl says
    # so after its atomic move, but a package that installs itself
    # whole (freedoom, gnuefi) does not, and read through the world
    # as it was, the install it just made was not there -- and got no
    # origin, no dependencies and no record. Said here, once, for
    # every kind of install.
    pkgmgr.installs_changed!

    # Read the world again: this build changed it. Every install of
    # this version, not just the first: gnuefi builds for i386, x86_64
    # AND noarch from one call, and recording only what find_install
    # happened to return left two thirds of it unverifiable -- and
    # an install with no origin reads as the default, manual, which
    # is right only by chance.
    pkg = pkg.at(action.scope, world: pkgmgr.world, versions: action.bound)
    for a in pkg.install_archs(ver)
      sc = a ? action.scope.with(arch: a) : action.scope
      i = pkg.at(sc, world: pkgmgr.world, versions: action.bound)
             .find_install(ver)
      next if i.nil?
      InstallOrigin.write(i.path, action.origin == :default,
                          action.mark == :manual)
      InstallDeps.write(i.path, action.against)
      pkg.at(sc, versions: action.bound).write_build_inputs(i)
    end
    pkgmgr.installs_changed!

    # The stacks this install defines, if any -- a compiler's -- said
    # where they live, so that nothing has to guess a stack's compiler
    # from its name.
    for c, m in pkg.stacks_defined(ver, action.against) do
      StackManifest.write(c, m)
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
