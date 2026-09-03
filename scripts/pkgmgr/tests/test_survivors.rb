# SPDX-License-Identifier: BSD-2-Clause
#
# TESTS THAT DID NOT EXIST.
#
# Each test here is a mutant that survived: a line of the logic core
# was made wrong on purpose (scripts/dev/claude/pmmutate) and the
# whole suite passed. Every one is named for the line it defends, and
# the comment says which way the line was wrong when nobody noticed.
#
# They are collected here rather than spread across the files whose
# subjects they touch so that the origin is plain: these are gaps the
# instrument found, not gaps a reader found. When one of them moves to
# a more natural home, the comment moves with it.
#

require_relative 'test_helper'
require_relative '../layout'

class TestSurvivors < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]
  V1   = Ver("1.0.0")
  V2   = Ver("2.0.0")

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # --- dep_resolver.rb ------------------------------------------------------

  # dep_resolver.rb:101  `graph[n] || []` -> `graph[n]`
  # A dependency that names a package nobody registered is listed, not
  # crashed on: validation is a separate question.
  def test_a_closure_lists_an_unregistered_dependency
    graph = { "a" => ["ghost"] }
    assert_equal ["ghost"], DepResolver.dep_closure("a", graph)
  end

  # dep_resolver.rb:59  `visit.call(dep) if color[dep] == white` -> `!=`
  # A cycle one hop away is still a cycle.
  def test_a_two_node_cycle_is_a_cycle
    graph = { "a" => ["b"], "b" => ["a"] }
    assert_raises(DepResolver::CycleError) {
      DepResolver.validate_no_cycles(graph)
    }
  end

  # dep_resolver.rb:52  `graph[node] || []` -> `graph[node]`
  # Validation reports a missing dependency by name rather than
  # falling over on it while walking for cycles.
  def test_validation_names_a_missing_dependency
    graph = { "a" => ["ghost"] }
    e = assert_raises(DepResolver::MissingDepError) {
      DepResolver.validate_deps(graph)
    }
    assert_match(/ghost/, e.message)
  end

  # --- version_solver.rb ----------------------------------------------------

  def solve(roots, deps, defaults, on_override: nil)
    VersionSolver.resolve(
      roots,
      deps_of: ->(n, _v) { deps[n] || [] },
      default_of: ->(n) { defaults[n] },
      on_override: on_override,
    )
  end

  # version_solver.rb:58  `on_override: nil` -> a non-callable default
  # Without a listener, a pin that moves a default is simply applied.
  def test_a_pin_needs_no_listener
    bound = solve([["a", nil]], { "a" => [Dep("b", true, ver: V2)] },
                  { "a" => V1, "b" => V1 })
    assert_equal V2, bound["b"]
  end

  # version_solver.rb:136  `default && default != dep.ver` -> drop `default`
  # A package with no default at all is not "overridden" by a pin:
  # there was nothing to override, and the listener is not told.
  def test_no_default_means_no_override_report
    told = []
    solve([["a", nil]], { "a" => [Dep("b", true, ver: V2)] },
          { "a" => V1 },
          on_override: ->(n, d, p, _path) { told << [n, d, p] })
    assert_empty told
  end

  # version_solver.rb:134  `!previous && on_override` -> `on_override`
  # Two pins that agree report the override once, on the first.
  def test_agreeing_pins_report_one_override
    told = []
    pin = [Dep("x", true, ver: V2)]
    deps = { "a" => pin, "b" => pin }
    solve([["a", nil], ["b", nil]], deps, { "a" => V1, "b" => V1, "x" => V1 },
          on_override: ->(n, _d, _p, _path) { told << n })
    assert_equal ["x"], told
  end

  # --- build_inputs.rb ------------------------------------------------------

  # build_inputs.rb:47  `return "missing" if !File.file?(path)` deleted
  # A patch that is not there is recorded as missing, not raised on.
  def test_a_missing_file_is_recorded_as_missing
    Dir.mktmpdir { |d|
      gone = Pathname(d) / "0001-not-here.diff"
      text = BuildInputs.render(recipe: "sha256:x", files: [gone])
      assert_match(/0001-not-here\.diff missing$/, text)
    }
  end

  # build_inputs.rb:79  `argv: nil` -> a non-nil default
  # A record written without an argv carries no argv line.
  def test_a_record_without_argv_has_no_argv_line
    Dir.mktmpdir { |d|
      BuildInputs.write(Pathname(d), recipe: "sha256:x", files: [])
      refute_match(/^argv/, File.read(File.join(d, BuildInputs::FILE)))
    }
  end

  # --- layout.rb ------------------------------------------------------------

  # layout.rb:64,66,69  `nil` -> "ALL" in the three host tiers' coords
  # The paths CMake is handed are the schema's, level by level.
  def test_the_layout_names_the_three_levels
    with_fake_tc do |tc|
      v = Layout.vars
      assert_equal (tc / HOST_OS_ARCH / "any" / "any" / "pkgs").to_s,
                   v["PKGS_HOST_PORTABLE"].to_s
      assert_equal (tc / HOST_OS_ARCH / HOST_DISTRO / "any" / "pkgs").to_s,
                   v["PKGS_HOST_DISTRO"].to_s
      assert_equal (tc / HOST_OS_ARCH / HOST_DISTRO / HOST_CC / "pkgs").to_s,
                   v["PKGS_HOST_CC"].to_s
      assert_equal (tc / "noarch" / "any" / "any" / "pkgs").to_s,
                   v["PKGS_NOARCH"].to_s
    end
  end

  # layout.rb:50  `return nil if arch.gcc_ver.nil?` deleted
  # An arch with no compiler version has no package tree to name, and
  # is left out rather than emitted with a hole in it.
  def test_an_arch_without_a_compiler_has_no_layout_entry
    with_fake_tc do
      saved = ALL_ARCHS["aarch64"].gcc_ver
      ALL_ARCHS["aarch64"].gcc_ver = nil
      begin
        v = Layout.vars
        refute v.key?("PKGS_TARGET_aarch64"), v.keys.inspect
        assert v.key?("PKGS_TARGET_i386")
      ensure
        ALL_ARCHS["aarch64"].gcc_ver = saved
      end
    end
  end

  # --- package.rb -----------------------------------------------------------

  # package.rb:318  `Coords.new("noarch", nil, nil)` -> "ALL" levels
  def test_a_noarch_package_lives_under_noarch_any_any
    with_fake_tc do |tc|
      p = FakePackage.new("src", arch_list: nil)
      assert_equal (tc / "noarch" / "any" / "any" / "pkgs" / "src" / "1.0.0"),
                   p.install_dir(V1)
    end
  end

  # package.rb:1155,1156  the two guards of default_cc
  # Each kind of package names its compiler its own way. Asked of the
  # BASE method: FakePackage overrides default_cc, and the fake's
  # answer for a target package says nothing about Package's.
  def test_default_cc_per_kind
    with_fake_tc do
      base = Package.instance_method(:default_cc)
      target = FakePackage.new("t")
      distro = FakePackage.new("host_d", on_host: true, host_tier: :distro,
                               arch_list: ALL_HOST_ARCHS.values)
      stack = FakePackage.new("host_s", on_host: true, host_tier: :stack,
                              arch_list: ALL_HOST_ARCHS.values)

      assert_equal FAKE_GCC_VER, base.bind(target).call
      assert_equal "syscc", base.bind(distro).call
      pkgmgr.with_host_stack(Ver("9.9.9")) {
        assert_equal Ver("9.9.9"), base.bind(stack).call
      }
    end
  end

  # A host package whose coordinates depend on the VERSION: host_gcc,
  # whose stack is its own version.
  class VersionedStackFake < TestHelper::FakePackage
    def initialize
      super("host_vs", on_host: true, host_tier: :stack,
            arch_list: ALL_HOST_ARCHS.values)
    end
    def stack_gcc_ver(ver = nil) = ver || pkgmgr.current_host_stack
  end

  # package.rb:423  `install_dir(ver) = pkg_dir_at(coords(ver))` -> coords()
  # package.rb:1440 `find_install: want = coords(ver)` -> coords()
  def test_coordinates_that_depend_on_the_version_are_asked_with_it
    with_fake_tc do
      p = VersionedStackFake.new
      pkgmgr.register(p)
      refute_equal p.install_dir(V1), p.install_dir(V2)

      fake_install(p, V2)
      assert_equal p.install_dir(V2), p.find_install(V2).path
      assert_nil p.find_install(V1)
    end
  end

  # package.rb:1477  `x.coords == want && !x.broken` -> drop `!x.broken`
  # A broken install is not a candidate for upgrading; it is broken.
  def test_a_broken_install_does_not_need_upgrading
    with_fake_tc do
      p = FakePackage.new("multi")
      p.define_singleton_method(:default_ver) { V2 }
      p.define_singleton_method(:expected_files) { |_v = nil|
        [["bin/thing", false]]
      }
      pkgmgr.register(p)

      dir = fake_install(p, V1)
      assert p.needs_upgrade?, "an old default install wants the new default"

      FileUtils.rm_f(dir / "bin" / "thing")
      pkgmgr.refresh
      refute p.needs_upgrade?, "broken: nothing to upgrade from"
    end
  end

  # A :compiler-tier package whose recipe reads the stack in effect.
  class StackReadingCompilerFake < TestHelper::FakePackage
    def initialize
      super("host_rc", on_host: true, host_tier: :compiler,
            arch_list: ALL_HOST_ARCHS.values)
    end
    def build_flags(ver = nil) = ["--stack=#{pkgmgr.current_host_stack}"]
  end

  # package.rb:737  `return block.call if host_tier != :stack` deleted
  # A :compiler-tier install has a gcc-* in its coordinates, but that
  # names the host's compiler: judging it must not move the stack.
  def test_a_compiler_tier_install_is_judged_without_moving_the_stack
    with_fake_tc do
      p = StackReadingCompilerFake.new
      pkgmgr.register(p)
      fake_install(p, V1)
      inst = p.find_install(V1)

      assert_equal :ok, p.build_inputs_state_of(inst)
      pkgmgr.with_host_stack(Ver("9.9.9")) {
        assert_equal :changed, p.build_inputs_state_of(inst),
                     "the recipe reads the stack, and the stack moved"
      }
    end
  end

  # package.rb:1583  `next if seen.include?(c)` deleted
  # When the current stack is also one on disk, it is listed once.
  def test_the_current_stack_is_scanned_once
    with_fake_tc do
      p = FakePackage.new("host_s", on_host: true, host_tier: :stack,
                          arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(p)
      fake_install(p, V1)
      paths = p.get_install_list.map(&:path)
      assert_equal paths.uniq, paths
    end
  end

  # package.rb:1651  `next if !cc_ver` deleted
  # A directory that starts with gcc- but names no version is not a
  # stack, and what is under it is not an installation.
  def test_a_gcc_directory_that_is_not_a_version_is_ignored
    with_fake_tc do |tc|
      p = FakePackage.new("t")
      pkgmgr.register(p)
      junk = tc / "tilck-i386" / "pc" / "gcc-notaversion" / "pkgs" / "t" /
             "1.0.0"
      FileUtils.mkdir_p(junk)
      assert_empty p.get_install_list
    end
  end

  # --- package_manager.rb ---------------------------------------------------

  # package_manager.rb:103  `@target_board = prev` -> `= nil`
  # A scope inside a scope restores the OUTER one, not nothing.
  def test_a_nested_board_scope_restores_the_outer
    pkgmgr.with_target_coords(RV, "licheerv-nano") {
      pkgmgr.with_target_coords(RV, "qemu-virt") {
        assert_equal "qemu-virt", pkgmgr.board_for(RV)
      }
      assert_equal "licheerv-nano", pkgmgr.board_for(RV)
    }
  end

  # package_manager.rb:215, 226  the support filters on upgradable and
  # stale packages: each conjunct alone decides for one package.
  def unsupported_trio
    by_arch = FakePackage.new("rv_only", arch_list: [RV])
    by_board = FakePackage.new("boardy", arch_list: [I386],
                               board_list: ["not-this-board"])
    by_host = FakePackage.new("host_elsewhere", on_host: true,
                              host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values,
                              host_os_list: ["plan9"])
    return [by_arch, by_board, by_host]
  end

  # package.rb  supported?  -- each conjunct decides alone.
  def test_supported_needs_all_three
    with_fake_tc do
      unsupported_trio.each { |p| refute p.supported?, p.name }
      assert FakePackage.new("plain").supported?
    end
  end

  # Each install sits where the CURRENT scope would look for it, so
  # that nothing but the support filter can be what excludes it. The
  # arch case cannot be built: a package's own scan visits only the
  # arches it supports, so an unsupported arch never has an install
  # to be asked about -- which is the same answer, reached earlier.
  def test_an_unsupported_package_is_not_upgradable_here
    with_fake_tc do
      for p in unsupported_trio.reject { |x| x.name == "rv_only" } do
        p.define_singleton_method(:default_ver) { V2 }
        pkgmgr.register(p)
        fake_install(p, V1)
      end
      assert_empty pkgmgr.get_upgradable_packages.map(&:name)
    end
  end

  def test_an_unsupported_package_is_not_stale_here
    with_fake_tc do
      for p in unsupported_trio do
        pkgmgr.register(p)
        at = p.on_host ? p.coords : p.coords(V1)
        at = Coords.new("tilck-riscv64", "qemu-virt", "gcc-#{FAKE_GCC_VER}") \
          if p.name == "rv_only"
        fake_install(p, V1, at: at, record: :changed)
      end
      assert_empty pkgmgr.get_stale_packages.map(&:name)
    end
  end

  # ...and a supported one with the same problems IS reported, so the
  # two tests above are not passing for want of installs.
  def test_a_supported_package_is_reported
    with_fake_tc do
      up = FakePackage.new("up")
      up.define_singleton_method(:default_ver) { V2 }
      stale = FakePackage.new("stale")
      pkgmgr.register(up)
      pkgmgr.register(stale)
      fake_install(up, V1)
      fake_install(stale, V1, record: :changed)

      assert_equal ["up"], pkgmgr.get_upgradable_packages.map(&:name)
      assert_equal ["stale"], pkgmgr.get_stale_packages.map(&:name)
    end
  end

  # package_manager.rb:232  `any?` -> `all?`
  # One stale install of a package makes the package stale.
  def test_one_stale_install_is_enough
    with_fake_tc do
      p = FakePackage.new("t")
      pkgmgr.register(p)
      fake_install(p, V1, record: :ok)
      other = Coords.new("tilck-riscv64", "qemu-virt", "gcc-#{FAKE_GCC_VER}")
      fake_install(p, V1, at: other, record: :changed)
      assert_equal ["t"], pkgmgr.get_stale_packages.map(&:name)
    end
  end

  # package_manager.rb:233  `!i.broken && ...` -> drop `!i.broken`
  # A broken install is not stale; it is broken, which is another list.
  def test_a_broken_install_is_not_stale
    with_fake_tc do
      p = FakePackage.new("t")
      p.define_singleton_method(:expected_files) { |_v = nil|
        [["bin/thing", false]]
      }
      pkgmgr.register(p)
      dir = fake_install(p, V1, record: :changed)
      FileUtils.rm_f(dir / "bin" / "thing")
      pkgmgr.refresh
      assert_empty pkgmgr.get_stale_packages
    end
  end

  # --- install_selector.rb --------------------------------------------------

  # install_selector.rb:65  the rendering of a selector
  def test_a_selector_says_what_it_means
    c = Coords.new("tilck-i386", "pc", "gcc-13")
    s = InstallSelector.new(name: "zlib", ver: V1,
                            where: [CoordsFilter.exact(c)])
    assert_equal "zlib:1.0.0 at tilck-i386/pc/gcc-13", s.to_s

    all = InstallSelector.new(name: :all, ver: :all, where: [CoordsFilter.any])
    assert_equal "ALL:ALL at any/any/any", all.to_s
  end
end

class TestSurvivorsToo < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]
  V1   = Ver("1.0.0")
  V2   = Ver("2.0.0")
  A    = Ver("7.7.7")
  B    = Ver("8.8.8")

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def tgt(arch, board = arch.default_board)
    Coords.new("tilck-#{arch.name}", board, "gcc-#{FAKE_GCC_VER}")
  end

  def stack_pkg(name, **kw)
    FakePackage.new(name, on_host: true, host_tier: :stack,
                    arch_list: ALL_HOST_ARCHS.values, **kw)
  end

  def distro_pkg(name)
    FakePackage.new(name, on_host: true, host_tier: :distro,
                    arch_list: ALL_HOST_ARCHS.values)
  end

  # --- package.rb: the kinds ---------------------------------------------

  # package.rb  target? / noarch?  -- each conjunct decides for one kind
  def test_the_three_kinds
    t = FakePackage.new("t")
    n = FakePackage.new("n", arch_list: nil)
    h = distro_pkg("host_h")
    assert t.target? && !t.noarch?
    assert n.noarch? && !n.target?
    refute h.target? || h.noarch?
  end

  # --- package_manager.rb: install's side effects -------------------------

  # A :stack package that puts something in the sysroot.
  class LibFake < TestHelper::FakePackage
    def initialize(name, stack: nil)
      super(name, on_host: true, host_tier: :stack,
            arch_list: ALL_HOST_ARCHS.values)
    end
    def expected_files(ver = nil) = [["install/usr/lib/libx.so", false]]

    # The stubbed harness builds nothing; this one leaves a library
    # behind, which is what the sysroot is composed from.
    def install_impl_internal(install_dir)
      lib = install_dir / "install" / "usr" / "lib"
      FileUtils.mkdir_p(lib)
      FileUtils.touch(lib / "libx.so")
      super
    end
  end

  # package_manager.rb:744  `if !pkg.sysroot_fragments(stack).empty?` -> `if`
  # Installing a stack package composes the stack's sysroot; installing
  # a distro package composes nothing.
  def test_a_stack_install_composes_its_sysroot
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(LibFake.new("host_lib"))
        pkgmgr.register(distro_pkg("host_d"))
        pkgmgr.with_host_stack(A) {
          pkgmgr.install("host_d")
          refute pkgmgr.stack_sysroot(A).directory?, "nothing to compose"

          pkgmgr.install("host_lib")
          assert (pkgmgr.stack_sysroot(A) / "usr" / "lib" / "libx.so").exist?
        }
      end
    end
  end

  # package_manager.rb:1384  `if removed > 0 && ...` -> recompose gated away
  # Removing a stack package recomposes: its entries leave the sysroot.
  def test_removing_a_stack_package_recomposes_the_sysroot
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(LibFake.new("host_lib"))
        pkgmgr.with_host_stack(A) {
          pkgmgr.install("host_lib")
          link = pkgmgr.stack_sysroot(A) / "usr" / "lib" / "libx.so"
          assert link.exist?
          pkgmgr.refresh
          pkgmgr.uninstall("host_lib", false, false)
          refute link.exist?, "the sysroot still names a removed package"
        }
      end
    end
  end

  # package_manager.rb:758  `if pkg.host_tier == :stack` -> `!=`
  # The portability audit runs on stack packages and on nothing else.
  def test_only_stack_packages_are_audited
    with_fake_tc do
      with_stubbed_externals do
        audited = []
        pkgmgr.define_singleton_method(:audit_portability) { |p, _v|
          audited << p.name
          true
        }
        begin
          pkgmgr.register(stack_pkg("host_s"))
          pkgmgr.register(distro_pkg("host_d"))
          pkgmgr.with_host_stack(A) {
            pkgmgr.install("host_s")
            pkgmgr.install("host_d")
          }
          assert_equal ["host_s"], audited
        ensure
          pkgmgr.singleton_class.send(:remove_method, :audit_portability)
        end
      end
    end
  end

  # package_manager.rb:785  `has_cc && pkg.target?` -> drop the kind
  # The implicit cross-compiler dependency belongs to target packages
  # only: a noarch package is source, and needs no compiler.
  def test_only_target_packages_depend_on_the_cross_compiler
    with_fake_tc do
      cc = FakePackage.new("gcc-i386-musl", on_host: true, is_compiler: true,
                           host_tier: :portable,
                           arch_list: ALL_HOST_ARCHS.values, target_arch: I386)
      pkgmgr.register(cc)
      pkgmgr.register(FakePackage.new("t"))
      pkgmgr.register(FakePackage.new("n", arch_list: nil))
      pkgmgr.register(distro_pkg("host_d"))

      g = pkgmgr.build_dep_graph
      assert_includes g["t"], "gcc-i386-musl"
      refute_includes g["n"], "gcc-i386-musl"
      refute_includes g["host_d"], "gcc-i386-musl"
    end
  end

  # --- package_manager.rb: clean ------------------------------------------

  # package_manager.rb:1151  the arch argument of clean's uninstall
  # --clean is every arch, not the scope's.
  def test_clean_takes_every_arch
    with_fake_tc do
      p = FakePackage.new("t")
      pkgmgr.register(p)
      fake_install(p, V1)
      fake_install(p, V1, at: tgt(RV))
      pkgmgr.clean(false)
      assert_empty p.get_install_list
    end
  end

  # --- package_manager.rb: the selector's edges ---------------------------

  def sel(pkg, list, **kw)
    pkgmgr.uninstall_selector(pkg, pkg&.name || "gone", list, **kw)
  end

  def installs(pkg, ver, *coords)
    coords.map { |c|
      InstallInfo.new(pkg&.name || "gone", nil, false, nil, ver,
                      c.pkgs_dir / "x" / ver.to_s, pkg, false, coords: c)
    }
  end

  # package_manager.rb:1188  `(all_pkgs || e.pkgname == name) && ...`
  # Whether the default is HERE is asked of this package's installs,
  # not of whatever else sits at the same coordinates.
  def test_another_packages_default_does_not_decide
    with_fake_tc do
      x = FakePackage.new("x")
      y = FakePackage.new("y")
      [x, y].each { |p| pkgmgr.register(p) }
      list = installs(x, V2, tgt(I386)) + installs(y, V1, tgt(I386))
      assert_equal :all, sel(x, list).ver,
                   "x's default is not here; y's presence is not x's"
    end
  end

  # package_manager.rb:1192  `ver.eql?("ALL") || (ver.nil? && pkg.nil?)`
  # An orphan asked for by version loses that version only.
  def test_an_orphan_named_by_version_loses_that_version_only
    with_fake_tc do
      list = installs(nil, V1, tgt(I386)) + installs(nil, V2, tgt(I386))
      s = sel(nil, list, ver: V1)
      assert_equal V1, s.ver
      assert_equal [V1], list.select { |i| s.matches?(i) }.map(&:ver)
    end
  end

  # package_manager.rb:1264  noarch with -a, -c V, -c ALL
  def test_noarch_flags
    with_fake_tc do
      n = FakePackage.new("n", arch_list: nil)
      pkgmgr.register(n)
      list = installs(n, V1, n.coords)
      assert_empty sel(n, list, arch: "riscv64").where
      assert_empty sel(n, list, compiler: Ver("9.9.9")).where
      assert_equal [CoordsFilter.exact(n.coords)],
                   sel(n, list, compiler: "ALL").where
    end
  end

  # package_manager.rb:1271,1272  a host package with -c ALL and -c V
  def test_host_compiler_flags
    with_fake_tc do
      d = distro_pkg("host_d")
      pkgmgr.register(d)
      list = installs(d, V1, d.coords)
      assert_equal [CoordsFilter.exact(d.coords)],
                   sel(d, list, compiler: "ALL").where
      assert_empty sel(d, list, compiler: Ver("9.9.9")).where,
                   "a non-stack package is not built by a stack"
    end
  end

  # package_manager.rb:1348  the "it is installed at" listing
  # names this package's installs and nobody else's.
  def test_nothing_matched_lists_only_that_package
    with_fake_tc do
      with_stubbed_externals do
        a = FakePackage.new("a")
        b = FakePackage.new("b")
        [a, b].each { |p| pkgmgr.register(p) }
        fake_install(a, V1, at: tgt(RV))
        fake_install(b, V1, at: tgt(ALL_ARCHS["x86_64"]))

        _, out = run_cli("-u", "a", "-q")
        assert_match(/installed at tilck-riscv64/, out)
        refute_match(/x86_64/, out, "another package's coordinates listed")
      end
    end
  end

  # package_manager.rb:291  `x.pkg&.is_compiler && x.ver == gcc_ver`
  # An installed cross compiler counts only at the version the arch is
  # configured to use; another version of it on disk is not "the"
  # compiler with_cc puts on PATH.
  def test_an_installed_compiler_is_one_at_the_configured_version
    with_fake_tc do
      cc = FakePackage.new("gcc-i386-musl", on_host: true, is_compiler: true,
                           host_tier: :portable,
                           arch_list: ALL_HOST_ARCHS.values, target_arch: I386)
      pkgmgr.register(cc)

      fake_install(cc, V1)
      assert_empty pkgmgr.get_installed_compilers, "1.0.0 is not 13.3.0"

      fake_install(cc, FAKE_GCC_VER)
      assert_equal [FAKE_GCC_VER],
                   pkgmgr.get_installed_compilers.map(&:ver)
    end
  end

  # --- main.rb:807  -H names a stack the compiler can build ---------------

  class TwoStackGcc < TestHelper::FakePackage
    def initialize
      super("host_gcc", on_host: true, host_tier: :distro,
            arch_list: ALL_HOST_ARCHS.values)
    end
    def default_ver = pkgmgr.current_host_stack
    def installable_versions = [Ver("7.7.7"), Ver("8.8.8")]
  end

  def test_an_unknown_stack_is_refused
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(TwoStackGcc.new)
        rc, out = run_cli("-H", "9.9.9", "-l", "-q")
        assert_equal 1, rc
        assert_match(/Unknown host GCC stack/, out)

        rc, _ = run_cli("-H", "gcc-8.8.8", "-l", "-q")
        assert_equal 0, rc
      end
    end
  end
end
