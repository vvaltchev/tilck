# SPDX-License-Identifier: BSD-2-Clause
#
# THE COMMAND LINE, NOT THE LIBRARY UNDER IT.
#
# test_uninstall.rb is one of the larger files here and it never
# caught any of the three -u bugs this tree has had, because it calls
# pkgmgr.uninstall(...) directly -- below the layer where all three
# lived. The whole suite drives Main.main 26 times, and not once with
# -u:
#
#   -s ...        9 invocations
#   -l ...        3
#   --upgrade     3
#   --check-for-updates  2
#   -C ...        2
#   -u ...        0
#
# The bugs were in the argument-computing layer: force_remove passing
# nil where it meant "any compiler", uninstall defaulting the compiler
# through a "syscc" test that stopped being true, -f running outside
# the stack scope. Every one of them is invisible from below and
# obvious from above.
#
# So these tests run the CLI and then look at the TREE. Not at what
# was printed -- at which installations still exist. A command that
# claims success while removing the wrong directory fails here.
#

require_relative 'test_helper'
require_relative '../main'
require 'stringio'

class TestCliMatrix < Minitest::Test

  include TestHelper

  RV = "riscv64"

  def setup
    reset_pkgmgr!
  end

  # Every installation on disk, by path. The assertions below compare
  # these sets, because "what survived" is the only question that
  # matters after a removal.
  def snapshot
    pkgmgr.refresh
    pkgmgr.all_packages.flat_map { |p|
      p.get_install_list.reject { |i| i.path.nil? }.map { |i| i.path.to_s }
    }.sort
  end

  # A target package installed for three arches, one of which has two
  # boards, plus a second version. Four coordinates and two versions,
  # which is enough for every filter to be wrong in a visible way.
  def install_target_spread(name = "spread")
    pkg = FakePackage.new(name)
    pkgmgr.register(pkg)

    with_context(ARCH: ALL_ARCHS["i386"], BOARD: nil) {
      pkgmgr.install(name)
    }
    with_context(ARCH: ALL_ARCHS["x86_64"], BOARD: nil) {
      pkgmgr.install(name)
    }
    with_context(ARCH: ALL_ARCHS[RV], BOARD: "qemu-virt") {
      pkgmgr.install(name)
    }
    with_context(ARCH: ALL_ARCHS[RV], BOARD: "licheerv-nano") {
      pkgmgr.install(name)
    }

    pkgmgr.refresh
    return pkg
  end

  def paths_for(pkg, &filter)
    pkgmgr.refresh
    pkg.get_install_list.reject { |i| i.path.nil? }
       .select { |i| filter.nil? || filter.call(i) }
       .map { |i| i.path.to_s }
  end

  # --- -u, the mode with no CLI test at all -------------------------

  # The default is the coordinates the invocation names, and nothing
  # else. `-u zlib` on riscv64 took the licheerv-nano copy with it for
  # as long as this tree has had two boards.
  def test_u_removes_only_the_current_coordinates
    with_fake_tc do
      with_stubbed_externals do
        pkg = install_target_spread
        before = snapshot

        rc, out = with_context(ARCH: ALL_ARCHS[RV], BOARD: "qemu-virt") {
          run_cli("-u", "spread", "-q")
        }
        assert_equal 0, rc, out

        gone = before - snapshot
        assert_equal 1, gone.length,
                     "removed #{gone.length} trees, not one:\n#{gone.join("\n")}"
        assert_includes gone.first, "qemu-virt"
      end
    end
  end

  def test_u_with_all_arches_removes_every_coordinate
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        rc, _ = run_cli("-u", "spread", "-a", "ALL", "-q")

        assert_equal 0, rc
        assert_empty snapshot.select { |p| p.include?("/spread/") },
                     "-a ALL left something behind"
      end
    end
  end

  # A dry run is a question, not a command.
  def test_u_dry_run_removes_nothing
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        before = snapshot

        rc, out = run_cli("-u", "spread", "-a", "ALL", "-d", "-q")

        assert_equal 0, rc
        assert_equal before, snapshot, "-d removed something"
        assert_match(/DRY RUN/, out)
      end
    end
  end

  # Naming a version that is not installed must remove nothing. It
  # once removed every version of the package instead.
  def test_u_with_an_absent_version_removes_nothing
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        before = snapshot

        rc, out = run_cli("-u", "spread:9.9.9", "-q")

        assert_equal 0, rc
        assert_equal before, snapshot,
                     "a version that is not installed took the ones that are"
        assert_match(/not installed|nothing matched/, out)
      end
    end
  end

  # A host :stack package lives under the compiler that built it, so
  # -H picks which installation the command means.
  def test_u_of_a_stack_package_follows_the_stack
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_thing", on_host: true,
                              host_tier: :stack,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)

        a = Ver("7.7.7")
        b = Ver("8.8.8")

        # -H names a stack, and a stack is named by a compiler: the
        # option checks the version against what the compiler package
        # says it can build.
        gcc = FakePackage.new("host_gcc", on_host: true,
                              host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values)
        gcc.define_singleton_method(:installable_versions) { [a, b] }
        pkgmgr.register(gcc)
        pkgmgr.with_host_stack(a) { pkgmgr.install("host_thing") }
        pkgmgr.with_host_stack(b) { pkgmgr.install("host_thing") }
        pkgmgr.refresh

        before = snapshot
        rc, _ = run_cli("-H", a.to_s, "-u", "host_thing", "-q")
        assert_equal 0, rc

        gone = before - snapshot
        assert_equal 1, gone.length, "removed #{gone.length} trees, not one"
        assert_includes gone.first, "gcc-7.7.7"
      end
    end
  end

  # --- -s ------------------------------------------------------------

  # -f is a removal followed by an install, and both halves have to
  # mean the same installation. When they did not, the run announced
  # a removal and then said "already installed" -- twice, for two
  # different reasons.
  def test_s_force_actually_reinstalls
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh

        FakePackage.clear_log!
        rc, out = run_cli("-s", "foo", "-f", "-q")

        assert_equal 0, rc
        assert_includes FakePackage.install_log, "foo",
                        "-f reported success without building anything"
        refute_match(/already installed/, out)
      end
    end
  end

  # --- the modes that had no CLI test at all --------------------------

  # -L answers "which stacks do I have", which -l cannot: -l groups
  # BY stack and buries the question under every package in each.
  def test_L_lists_stacks_built_and_not
    with_fake_tc do
      with_stubbed_externals do
        a = Ver("7.7.7")
        b = Ver("8.8.8")

        gcc = FakePackage.new("host_gcc", on_host: true,
                              host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values)
        gcc.define_singleton_method(:installable_versions) { [a, b] }
        gcc.define_singleton_method(:default_ver) { a }
        pkgmgr.register(gcc)
        pkgmgr.install("host_gcc", a)
        pkgmgr.refresh

        _, out = run_cli("-L", "-q")
        plain = out.gsub(/\e\[[0-9;]*m/, "")

        assert_match(/gcc-7\.7\.7\s+\[\s*built\s*\]/, plain)
        assert_match(/gcc-8\.8\.8\s+\[\s*not built\s*\]/, plain)
      end
    end
  end

  # --print-layout is what CMake asks at every configure. It must
  # print KEY=value and nothing that needs interpreting.
  def test_print_layout_is_machine_readable
    with_fake_tc do
      _, out = run_cli("--print-layout", "-q")
      lines = out.lines.map(&:chomp).reject(&:empty?)
                 .select { |l| l =~ /\A[A-Z_0-9]+=/ }

      keys = lines.map { |l| l.split("=", 2).first }
      for k in %w[ARCH BOARD TCROOT PKGS_TARGET PKGS_NOARCH] do
        assert_includes keys, k, "#{k} is not in --print-layout"
      end

      # Every emitted path must be inside the toolchain being asked
      # about, or CMake builds against a tree nobody created.
      for l in lines.select { |x| x.start_with?("PKGS_") } do
        assert_includes l.split("=", 2).last, TC.to_s
      end
    end
  end

  # --list-installable feeds tooling: one name per line, no decoration.
  def test_list_installable_is_bare_names
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("alpha"))
        pkgmgr.register(FakePackage.new("beta"))

        _, out = run_cli("--list-installable", "-q")
        names = out.lines.map { |l| l.split.first }

        assert_includes names, "alpha"
        assert_includes names, "beta"
        refute_match(/\e\[/, out, "decoration in machine-readable output")
      end
    end
  end

  # -D prints the dependency tree, and the whole point is that it
  # shows the transitive ones.
  def test_D_shows_the_transitive_tree
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("leaf"))
        pkgmgr.register(FakePackage.new("mid", dep_list: [Dep("leaf", false)]))
        pkgmgr.register(FakePackage.new("top", dep_list: [Dep("mid", false)]))

        _, out = run_cli("-D", "top", "--ascii", "-q")

        assert_match(/top/, out)
        assert_match(/mid/, out, "the direct dependency is missing")
        assert_match(/leaf/, out, "the transitive dependency is missing")
      end
    end
  end

  # --clean takes everything a rebuild would recreate, and leaves the
  # things a rebuild cannot: the prebuilt compilers, and Ruby, which
  # the package manager is running on.
  def test_clean_empties_the_tree_but_spares_ruby
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("ordinary"))
        pkgmgr.install("ordinary")

        # Ruby is not a registered package: it is the interpreter the
        # package manager bootstrapped itself with, found on disk. It
        # is protected by name, which is the only handle there is.
        ruby_path = distro_pkgs / "ruby" / "3.4.7"
        FileUtils.mkdir_p(ruby_path)
        pkgmgr.refresh

        rc, _ = run_cli("--clean", "-q")
        assert_equal 0, rc

        assert_empty snapshot.select { |p| p.include?("/ordinary/") },
                     "--clean left an ordinary package behind"
        assert ruby_path.directory?,
               "--clean removed the Ruby it is running on"
      end
    end
  end

  # -S and -U name an ARCH, and an unknown one has to be refused
  # rather than resolved to something nearby.
  def test_S_and_U_refuse_an_unknown_arch
    with_fake_tc do
      for flag in ["-S", "-U"] do
        rc, out = run_cli(flag, "notanarch", "-q")
        refute_equal 0, rc, "#{flag} accepted an arch that does not exist"
        assert_match(/rch/, out, "#{flag} did not say what was wrong")
      end
    end
  end

  # --- dry run touches nothing ----------------------------------------

  # Every file under the toolchain, with its size and mtime. A dry run
  # may create an empty directory -- the tree is laid out before the
  # mode runs -- but it may not add, remove or modify a single file.
  def file_fingerprint
    Dir.glob("#{TC}/**/*", File::FNM_DOTMATCH)
       .reject { |p| File.directory?(p) }
       .sort
       .map { |p| [p.sub("#{TC}/", ""), File.size(p), File.mtime(p).to_f] }
  end

  # THE rule this file exists to enforce.
  #
  # -d is what you run before doing something you cannot undo, so it
  # has to be trustworthy in every mode, not most of them. -C ignored
  # it outright until this test went looking; the others are checked
  # here so that the next mode to grow a write is caught by the same
  # net rather than by someone's memory.
  def test_every_destructive_mode_touches_nothing_in_dry_run
    modes = {
      "install"            => ["-s", "spread"],
      "install forced"     => ["-s", "spread", "-f"],
      "install all arches" => ["-s", "spread", "-a", "ALL"],
      "uninstall"          => ["-u", "spread"],
      "uninstall version"  => ["-u", "spread:1.0.0"],
      "uninstall all"      => ["-u", "spread", "-a", "ALL"],
      "clean"              => ["--clean"],
      "upgrade"            => ["--upgrade"],
    }

    for what, argv in modes do
      with_fake_tc do
        with_stubbed_externals do
          reset_pkgmgr!
          install_target_spread
          before = file_fingerprint

          rc, _ = run_cli(*argv, "-d", "-q")

          assert_equal 0, rc, "#{what}: -d failed"
          assert_equal before, file_fingerprint,
                       "#{what} -d changed the tree"
        end
      end
    end
  end

  # -C runs a package's own configuration tool and rewrites its build
  # configuration, which is exactly the kind of thing -d is for. It
  # ignored the flag entirely.
  def test_reconfigure_honours_dry_run
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("confy")
        pkg.define_singleton_method(:configurable?) { true }
        pkg.define_singleton_method(:configure) { |ver = nil|
          raise "configure ran during a dry run"
        }
        pkgmgr.register(pkg)
        pkgmgr.install("confy")
        pkgmgr.refresh

        rc, out = run_cli("-C", "confy", "-d", "-q")

        assert_equal 0, rc
        assert_match(/Dry run/, out)
      end
    end
  end

  # --- the host world ------------------------------------------------

  # The compiler we build ourselves and the QEMU built with it are
  # expensive and rarely wanted. The system tests exist to prove that
  # the packages Tilck is built FROM build on this machine, so they
  # skip that world -- and --list-installable is what tells them
  # which packages it is.
  def test_list_installable_marks_the_host_world
    with_fake_tc do
      with_stubbed_externals do
        base = FakePackage.new("ordinary")
        gcc = FakePackage.new("host_gcc", on_host: true,
                              host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values)
        dep = FakePackage.new("host_only_for_gcc", on_host: true,
                              host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values)
        gcc.define_singleton_method(:host_world_root?) { true }
        gcc.define_singleton_method(:dep_list) { [Dep("host_only_for_gcc", true)] }

        [base, gcc, dep].each { |p| pkgmgr.register(p) }

        _, out = run_cli("--list-installable", "-q")
        tags = out.lines.map(&:split).select { |f| f.length == 2 }.to_h

        assert_equal "host-world", tags["host_gcc"]
        assert_equal "host-world", tags["host_only_for_gcc"],
                     "a package only the root needs is part of its world"
        refute_equal "host-world", tags["ordinary"],
                     "an ordinary package was swept into the host world"
      end
    end
  end

  # ...and a package the host world shares with something Tilck needs
  # is NOT part of it. Otherwise the first target package to want zlib
  # would quietly stop being installed by the system tests.
  def test_a_shared_dependency_is_not_host_world
    with_fake_tc do
      with_stubbed_externals do
        shared = FakePackage.new("host_shared", on_host: true,
                                 host_tier: :distro,
                                 arch_list: ALL_HOST_ARCHS.values)
        gcc = FakePackage.new("host_gcc", on_host: true,
                              host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values)
        gcc.define_singleton_method(:host_world_root?) { true }
        gcc.define_singleton_method(:dep_list) { [Dep("host_shared", true)] }

        tilck = FakePackage.new("target_thing",
                                dep_list: [Dep("host_shared", true)])

        [shared, gcc, tilck].each { |p| pkgmgr.register(p) }

        refute_includes pkgmgr.host_world_names, "host_shared",
                        "a dependency Tilck also needs was called host-world"
        assert_includes pkgmgr.host_world_names, "host_gcc"
      end
    end
  end

  # --clean leaves what it is told to leave. The system tests wipe the
  # tree to reinstall it, and rebuilding a compiler and a GTK stack to
  # prove that busybox builds is hours spent on a different question.
  def test_clean_can_spare_the_host_world
    with_fake_tc do
      with_stubbed_externals do
        keep = FakePackage.new("host_keeper", on_host: true,
                               host_tier: :distro,
                               arch_list: ALL_HOST_ARCHS.values)
        drop = FakePackage.new("ordinary")
        pkgmgr.register(keep)
        pkgmgr.register(drop)
        pkgmgr.install("host_keeper")
        pkgmgr.install("ordinary")
        pkgmgr.refresh

        kept = keep.get_install_list.find { |i| !i.path.nil? }.path
        pkgmgr.clean(false, except: ["host_keeper"])
        pkgmgr.refresh

        assert kept.directory?, "--clean removed what it was told to keep"
        assert_empty snapshot.select { |p| p.include?("/ordinary/") }
      end
    end
  end

  # --- -l --------------------------------------------------------------  # --- -l --------------------------------------------------------------

  def test_l_shows_every_arch_an_install_exists_for
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        _, out = run_cli("-l", "-q")
        plain = out.gsub(/\e\[[0-9;]*m/, "")

        line = plain.lines.find { |l| l.start_with?("spread") }
        refute_nil line, "the package is not listed at all"

        for arch in ["i386", "x86_64", RV] do
          assert_includes line, arch, "#{arch} is missing from the line"
        end
      end
    end
  end

  def test_l_group_by_ver_keeps_the_versions_distinct
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        _, out = run_cli("-l", "-g", "ver", "-q")
        plain = out.gsub(/\e\[[0-9;]*m/, "")

        line = plain.lines.find { |l| l.start_with?("spread") }
        assert_includes line, "1.0.0"
      end
    end
  end
end
