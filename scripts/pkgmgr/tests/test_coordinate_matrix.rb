# SPDX-License-Identifier: BSD-2-Clause
#
# TWO OF EVERYTHING, ONE AXIS AT A TIME.
#
# More than half the bugs this package manager has had are one bug:
# an operation that acts on a specific installation, performed without
# saying which one it means. find_install comparing versions instead
# of Coords; force_remove taking every board of an arch, then every
# version of a package; the staleness check reading a stack package's
# flags at whichever stack happened to be current; -f removing a tree
# nobody was rebuilding. Each was invisible because the wrong answer
# is a plausible one -- the code returns AN install, just not the one
# asked for.
#
# They survive because a fixture with one install cannot tell "the
# right one" from "the only one". So this file is a matrix: for every
# axis an installation can differ along, two REAL installs that differ
# in exactly that and nothing else, and every operation asked to touch
# one of them.
#
#   axis      the two installs differ in
#   -------   ---------------------------------------------
#   version   1.0.0 and 2.0.0, same coordinates
#   arch      i386 and x86_64
#   board     riscv64/qemu-virt and riscv64/licheerv-nano
#   stack     gcc-<A> and gcc-<B>, a host :stack package
#
# Real installs, not mkdir_p: a bare directory carries no
# .build_inputs, so it cannot answer a staleness question, and half
# the operations here are about exactly that.
#

require_relative 'test_helper'

class TestCoordinateMatrix < Minitest::Test

  include TestHelper

  V1 = Ver("1.0.0")
  V2 = Ver("2.0.0")

  STACK_A = Ver("7.7.7")
  STACK_B = Ver("8.8.8")

  #
  # The pair packages, and the only two things that separate them from
  # a plain FakePackage.
  #
  # build_env: one of the operations below is a DEPENDENT reading the
  # interface this publishes, and the base class publishes nothing.
  # It is built from install_prefix, which resolves through
  # find_install at the current coordinates -- so what a dependent
  # sees is a function of the ambient scope, which is the subject.
  #
  # build_flags: the recipe has to READ differently at each install's
  # coordinates, the way a real one does (zlib names its archiver
  # "#{default_arch.gcc_tc}-linux-ar", i686-linux-ar for one install
  # of a version and riscv64-linux-ar for another). With a recipe that
  # renders identically everywhere, an install stamped with another's
  # record is byte-identical to a correct one, and the whole question
  # of WHICH install a record belongs to becomes untestable.
  #
  class MatrixPkg < TestHelper::FakePackage

    def build_flags(ver = nil) = ["--ver=#{ver}", "--at=#{coords()}"]

    def build_env(ver)
      prefix = install_prefix(ver)
      return BuildEnv.new(include_dirs: [prefix / "include"],
                          lib_dirs:     [prefix / "lib"])
    end
  end

  # What an axis hands back: the package, the two installs, and a way
  # to open the ambient context each of them lives in. `scope` is what
  # every one of these operations reads implicitly when nobody tells
  # it otherwise -- which is the whole subject.
  #
  # dep_view_b: which of the two a DEPENDENT sees under scope_b.
  #
  # Normally B -- the scope is what selects the install. The version
  # axis is the exception, and not an accident: a dependent cannot pin
  # a target package to a version (check_dep_pins: Tilck is built from
  # exactly one version of each package), so the resolver hands out
  # the default in both scopes, which is A. The property under test
  # holds either way -- exactly ONE of the two installs is named, and
  # never a mixture of both.
  #
  Pair = Struct.new(:pkg, :a, :b, :scope_a, :scope_b, :ver_a, :ver_b,
                    :dep_view_b, keyword_init: true)

  def inst_of(pkg, ver, &scope)
    scope.call {
      pkg.get_install_list.find { |i|
        i.ver == ver && !i.path.nil? && i.coords == pkg.coords(ver)
      }
    }
  end

  def here = ->(&blk) { blk.call }

  # --- the axes --------------------------------------------------------

  def axis_version
    pkg = MatrixPkg.new("multi")
    pkgmgr.register(pkg)
    pkgmgr.install("multi", V1)
    pkgmgr.install("multi", V2)
    pkgmgr.refresh

    Pair.new(
      pkg: pkg, ver_a: V1, ver_b: V2,
      a: inst_of(pkg, V1, &here), b: inst_of(pkg, V2, &here),
      scope_a: here, scope_b: here, dep_view_b: :a,
    )
  end

  def axis_arch
    pkg = MatrixPkg.new("archy")
    pkgmgr.register(pkg)

    i386 = ->(&blk) { with_context(ARCH: ALL_ARCHS["i386"], BOARD: nil, &blk) }
    x64 = ->(&blk) { with_context(ARCH: ALL_ARCHS["x86_64"], BOARD: nil, &blk) }

    i386.call { pkgmgr.install("archy") }
    x64.call { pkgmgr.install("archy") }
    pkgmgr.refresh

    Pair.new(
      pkg: pkg, ver_a: V1, ver_b: V1,
      a: inst_of(pkg, V1, &i386), b: inst_of(pkg, V1, &x64),
      scope_a: i386, scope_b: x64, dep_view_b: :b,
    )
  end

  def axis_board
    pkg = MatrixPkg.new("boardy")
    pkgmgr.register(pkg)

    rv = ALL_ARCHS["riscv64"]
    qemu = ->(&blk) { with_context(ARCH: rv, BOARD: "qemu-virt", &blk) }
    lichee = ->(&blk) { with_context(ARCH: rv, BOARD: "licheerv-nano", &blk) }

    qemu.call   { pkgmgr.install("boardy") }
    lichee.call { pkgmgr.install("boardy") }
    pkgmgr.refresh

    Pair.new(
      pkg: pkg, ver_a: V1, ver_b: V1,
      a: inst_of(pkg, V1, &qemu), b: inst_of(pkg, V1, &lichee),
      scope_a: qemu, scope_b: lichee, dep_view_b: :b,
    )
  end

  def axis_stack
    pkg = MatrixPkg.new("host_stacky", on_host: true,
                        host_tier: :stack,
                        arch_list: ALL_HOST_ARCHS.values)
    pkgmgr.register(pkg)

    sa = ->(&blk) { pkgmgr.with_host_stack(STACK_A, &blk) }
    sb = ->(&blk) { pkgmgr.with_host_stack(STACK_B, &blk) }

    sa.call { pkgmgr.install("host_stacky") }
    sb.call { pkgmgr.install("host_stacky") }
    pkgmgr.refresh

    Pair.new(
      pkg: pkg, ver_a: V1, ver_b: V1,
      a: inst_of(pkg, V1, &sa), b: inst_of(pkg, V1, &sb),
      scope_a: sa, scope_b: sb, dep_view_b: :b,
    )
  end

  AXES = [:version, :arch, :board, :stack].freeze

  def with_axis(axis)
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        p = send("axis_#{axis}")

        refute_nil p.a, "#{axis}: the first install did not happen"
        refute_nil p.b, "#{axis}: the second install did not happen"
        refute_equal p.a.path, p.b.path,
                     "#{axis}: both installs landed in one place, so " \
                     "this axis tests nothing"

        yield p
      end
    end
  end

  # Everything under `dir`, by relative path and content.
  #
  # Content, not mtime: a rewrite producing identical bytes changes
  # nothing that anyone can read afterwards. The recipes here differ
  # per install (see MatrixPkg#build_flags) precisely so that a record
  # written for the wrong install does NOT come out identical.
  def fingerprint(dir)

    out = {}

    for f in Dir.glob("#{dir}/**/*", File::FNM_DOTMATCH).sort do
      next if File.directory?(f)
      rel = Pathname(f).relative_path_from(dir).to_s
      out[rel] = Digest::SHA256.file(f).hexdigest
    end

    return out
  end

  # A package that depends on `pkg`, so deps_build_env has something
  # to resolve. Registered after both installs exist: the dependency
  # graph is built on demand, so nothing needed to know earlier.
  #
  # It mirrors the pair package's tier and arch list rather than
  # taking the defaults -- a :stack dependent of a :stack package is
  # the real shape, and the one whose sysroot went empty.
  def dependent_on(pkg)

    dep = TestHelper::FakePackage.new(
      "#{pkg.name}_user",
      on_host: pkg.on_host,
      host_tier: pkg.host_tier,
      arch_list: pkg.arch_list,
      dep_list: [Dep(pkg.name, pkg.on_host)],
    )

    pkgmgr.register(dep)
    return dep
  end

  # --- the shape of the API itself -------------------------------------

  # The version-keyed staleness API is gone, and must stay gone.
  #
  # build_inputs_state(ver) and build_inputs_changed?(ver) looked up
  # find_install(ver) at the CURRENT coordinates and answered about
  # whatever came back. A caller holding one installation would ask
  # about a version and get an answer about another -- plausible,
  # wrong, and silent. The listing did exactly that.
  #
  # A lint could have watched for the misuse. Deleting the method is
  # better: it makes the question unaskable without naming the
  # installation, so the mistake cannot be written rather than merely
  # being caught after it is.
  def test_staleness_cannot_be_asked_by_version
    refute_includes Package.instance_methods, :build_inputs_state,
                    "the version-keyed staleness API is back"

    params = Package.instance_method(:build_inputs_changed?).parameters
    assert_equal [[:req, :inst]], params,
                 "build_inputs_changed? takes an install, not a version"
  end

  # ...and the one that survives takes the install, so it can be
  # judged where it lives.
  def test_the_surviving_api_takes_an_install
    params = Package.instance_method(:build_inputs_state_of).parameters
    assert_equal [[:req, :inst]], params
  end

  # A recipe that reads its board's BSP -- u-boot copies its .config
  # out of that directory -- must get the board of the installation it
  # is building or being judged against, not whatever board the
  # invocation happens to be set to.
  #
  # The global board_bsp() answers for the ARCH/BOARD pair the run
  # started with, which is right for the startup check that uses it
  # and wrong inside with_install_context. Package#board_bsp is the
  # one a recipe sees.
  def test_board_bsp_follows_the_install_not_the_invocation
    with_axis(:board) do |p|
      from_own = p.scope_b.call { p.pkg.board_bsp }
      from_other = p.scope_a.call {
        p.pkg.with_install_context(p.b) { p.pkg.board_bsp }
      }

      assert_equal "licheerv-nano", from_own.basename.to_s
      assert_equal from_own, from_other,
                   "the BSP path followed the invocation's board"
      refute_equal from_own, p.scope_a.call { p.pkg.board_bsp },
                   "both boards resolve to one BSP, so this proves nothing"
    end
  end

  # --- the operations, once per axis -----------------------------------

  AXES.each do |axis|

    # The lookup everything else is built on.
    define_method("test_#{axis}_find_install_returns_its_own") do
      with_axis(axis) do |p|
        found = p.scope_a.call { p.pkg.find_install(p.ver_a) }
        refute_nil found, "#{axis}: found nothing in its own scope"
        assert_equal p.a.path, found.path,
                     "#{axis}: find_install answered about the other one"
      end
    end

    # -u removes what the user is looking at, and nothing else.
    define_method("test_#{axis}_uninstall_leaves_the_other_alone") do
      with_axis(axis) do |p|
        p.scope_a.call {
          pkgmgr.uninstall(p.pkg.name, false, false, p.ver_a)
        }
        pkgmgr.refresh

        refute p.a.path.directory?, "#{axis}: -u removed nothing"
        assert p.b.path.directory?, "#{axis}: -u took the other one too"
      end
    end

    # -f removes exactly the tree the install is about to recreate.
    define_method("test_#{axis}_force_remove_leaves_the_other_alone") do
      with_axis(axis) do |p|
        p.scope_a.call { pkgmgr.force_remove(p.pkg.name, p.ver_a) }
        pkgmgr.refresh

        refute p.a.path.directory?, "#{axis}: -f removed nothing"
        assert p.b.path.directory?, "#{axis}: -f took the other one too"
      end
    end

    # A recipe is only a recipe AT some coordinates. Judging one
    # install from the other's scope has to give the same answer as
    # judging it from its own, or a package built minutes ago reads
    # as stale.
    define_method("test_#{axis}_staleness_is_judged_where_it_lives") do
      with_axis(axis) do |p|
        from_own = p.scope_b.call { p.pkg.build_inputs_state_of(p.b) }
        from_other = p.scope_a.call { p.pkg.build_inputs_state_of(p.b) }

        assert_equal :ok, from_own,
                     "#{axis}: a fresh install is not ok in its own scope"
        assert_equal from_own, from_other,
                     "#{axis}: judged differently from the other scope"
      end
    end

    # Both are installs of one package, and the listing has to show
    # both however the invocation is scoped.
    define_method("test_#{axis}_both_are_listed") do
      with_axis(axis) do |p|
        paths = p.scope_a.call {
          p.pkg.get_install_list.reject { |i| i.path.nil? }.map(&:path)
        }

        assert_includes paths, p.a.path, "#{axis}: its own is missing"
        assert_includes paths, p.b.path, "#{axis}: the other is missing"
      end
    end

    # An install writes under its own coordinates and nowhere else.
    #
    # The tempting shape is to stamp every install of the version at
    # once, which is what recording "the arch" rather than "this arch"
    # amounts to. It certifies trees this build never touched against
    # the recipe as it reads today, so a binary built from something
    # else reports :ok forever -- worse than not recording at all,
    # because it is a wrong answer where there had been none.
    define_method("test_#{axis}_installing_one_leaves_the_other_intact") do
      with_axis(axis) do |p|
        before = fingerprint(p.b.path)

        p.scope_a.call {
          pkgmgr.force_remove(p.pkg.name, p.ver_a)
          pkgmgr.install(p.pkg.name, p.ver_a)
        }
        pkgmgr.refresh

        assert p.a.path.directory?, "#{axis}: the reinstall did not happen"
        assert_equal before, fingerprint(p.b.path),
                     "#{axis}: installing one rewrote the other"
      end
    end

    # ...and the other one must not answer "is it installed?" on its
    # behalf. When it does, -s reports everything already installed
    # and builds nothing, which is a no-op that looks like a success.
    define_method("test_#{axis}_the_other_does_not_count_as_installed") do
      with_axis(axis) do |p|
        p.scope_a.call { pkgmgr.force_remove(p.pkg.name, p.ver_a) }
        pkgmgr.refresh

        plan = p.scope_a.call {
          pkgmgr.resolve_install_plan([[p.pkg.name, p.ver_a]])
        }
        assert_includes plan.map(&:first), p.pkg.name,
                        "#{axis}: the plan skipped a package that is not " \
                        "installed here, because the other one is"

        # The converse, so that the assertion above cannot pass by
        # the plan simply never skipping anything.
        done = p.scope_b.call {
          pkgmgr.resolve_install_plan([[p.pkg.name, p.ver_b]])
        }
        assert_empty done, "#{axis}: the plan rebuilds an install that is " \
                           "already there"
      end
    end

    # The record is read from the install it describes, not from
    # whatever the current coordinates happen to point at.
    define_method("test_#{axis}_a_broken_record_is_local_to_its_install") do
      with_axis(axis) do |p|
        rec_a = p.a.path / BuildInputs::FILE
        rec_b = p.b.path / BuildInputs::FILE

        assert rec_a.file?, "#{axis}: the first install recorded nothing"
        assert rec_b.file?, "#{axis}: the second install recorded nothing"
        refute_equal File.read(rec_a), File.read(rec_b),
                     "#{axis}: both installs recorded the same recipe, so " \
                     "a record read from the wrong one cannot be seen"

        File.write(rec_a, "recipe sha256:not-what-it-was-built-from\n")

        for scope in [p.scope_a, p.scope_b] do
          assert_equal :changed,
                       scope.call { p.pkg.build_inputs_state_of(p.a) },
                       "#{axis}: a clobbered record still reads as ok"
          assert_equal :ok,
                       scope.call { p.pkg.build_inputs_state_of(p.b) },
                       "#{axis}: one install's broken record condemned " \
                       "the other"
        end
      end
    end

    # A dependent is built against the install its own scope resolves
    # to. This is the sysroot bug's axis: composing one stack from an
    # invocation scoped to another found no fragments and emptied five
    # sysroots. The variant that finds the OTHER stack's fragments
    # instead is quieter and worse -- it links a foreign library and
    # says nothing.
    define_method("test_#{axis}_a_dependent_reads_the_resolved_install") do
      with_axis(axis) do |p|
        user = dependent_on(p.pkg)
        views = [[p.scope_a, p.a], [p.scope_b, p.send(p.dep_view_b)]]

        for scope, want in views do
          other = want.equal?(p.a) ? p.b : p.a
          dirs = scope.call { user.deps_build_env.include_dirs }

          assert_includes dirs, (want.path / "include").to_s,
                          "#{axis}: the dependent got no path from the " \
                          "install its scope resolves to"
          refute_includes dirs, (other.path / "include").to_s,
                          "#{axis}: the dependent would be built against " \
                          "the other install"
        end
      end
    end
  end
end
