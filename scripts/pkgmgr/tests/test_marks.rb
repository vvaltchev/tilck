# SPDX-License-Identifier: BSD-2-Clause
#
# MANUAL AND AUTOMATIC INSTALLS.
#
# Every install carries a mark: manual when it was asked for by name,
# auto when it came in as somebody's dependency. --autoremove takes
# the auto installs nothing kept needs; --mark-manual and --mark-auto
# move an install from one side to the other, selecting exactly what
# -u would. The mark is the second word of .install_origin, beside
# the default/pinned one; a one-word record predates it and reads as
# manual, which every install was until there was a way to say
# otherwise.
#

require_relative 'test_helper'

class TestMarks < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  X64  = ALL_ARCHS["x86_64"]
  RV   = ALL_ARCHS["riscv64"]

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # The mark of each install of `pkg`, by version and coordinates.
  def marks(pkg)
    pkgmgr.refresh
    pkg.get_install_list.reject { |i| i.path.nil? }
       .to_h { |i| [[i.ver.to_s, i.coords], i.manual ? :manual : :auto] }
  end

  def mark_of(pkg, ver = "1.0.0")
    m = marks(pkg).select { |(v, _), _| v == ver }.values.uniq
    assert_equal 1, m.length, "#{pkg.name}:#{ver}: #{m.inspect}"
    return m.first
  end

  def chain(a, b)
    pa = FakePackage.new(a, dep_list: [Dep(b, false)])
    pb = FakePackage.new(b)
    pkgmgr.register(pa)
    pkgmgr.register(pb)
    return [pa, pb]
  end

  # --- what -s writes -------------------------------------------------

  def test_a_requested_package_is_manual_and_its_dependencies_are_auto
    with_fake_tc do
      with_stubbed_externals do
        a, b = chain("a", "b")
        rc, _ = run_cli("-s", "a", "-q")
        assert_equal 0, rc
        assert_equal :manual, mark_of(a)
        assert_equal :auto, mark_of(b)
      end
    end
  end

  # `-s host_glib2` after `-s host_qemu`: glib2 is the user's now, and
  # must stand when qemu goes, even though nothing was installed.
  def test_asking_for_a_dependency_makes_it_manual
    with_fake_tc do
      with_stubbed_externals do
        _, b = chain("a", "b")
        run_cli("-s", "a", "-q")
        assert_equal :auto, mark_of(b)

        rc, out = run_cli("-s", "b", "-q")
        assert_equal 0, rc
        assert_match(/Set b:1.0.0 to manually installed/, out)
        assert_equal :manual, mark_of(b)

        _, again = run_cli("-s", "b", "-q")
        refute_match(/Set b/, again, "already the user's: nothing to say")
      end
    end
  end

  # ...at the version the request means: a pin beside it may have
  # moved the dependency off its default, and it is THAT install that
  # becomes the user's.
  def test_asking_for_a_pinned_dependency_claims_the_pinned_version
    with_fake_tc do
      with_stubbed_externals do
        a = FakePackage.new("host_a", on_host: true, host_tier: :distro,
                            arch_list: ALL_HOST_ARCHS.values,
                            dep_list: [Dep("host_x", true,
                                           ver: Ver("2.0.0"))])
        x = FakePackage.new("host_x", on_host: true, host_tier: :distro,
                            arch_list: ALL_HOST_ARCHS.values)
        x.define_singleton_method(:installable_versions) {
          [Ver("1.0.0"), Ver("2.0.0")]
        }
        pkgmgr.register(a)
        pkgmgr.register(x)
        fake_install(x, "2.0.0", origin: :pinned, mark: :auto)

        rc, _ = run_cli("-s", "host_a", "host_x", "-q")
        assert_equal 0, rc
        assert_equal :manual, mark_of(x, "2.0.0")
        assert_equal ["2.0.0"], marks(x).keys.map(&:first),
                     "the default 1.0.0 was not what was asked for"
      end
    end
  end

  # --- --mark-auto / --mark-manual ------------------------------------

  def test_mark_flips_one_install_and_takes_a_version
    with_fake_tc do
      with_stubbed_externals do
        a, _ = chain("a", "b")
        run_cli("-s", "a", "-q")

        rc, out = run_cli("--mark-auto", "a", "-q")
        assert_equal 0, rc
        assert_match(/Mark a:1.0.0 at .* as automatically installed/, out)
        assert_equal :auto, mark_of(a)

        rc, _ = run_cli("--mark-manual", "a:1.0.0", "-q")
        assert_equal 0, rc
        assert_equal :manual, mark_of(a)
      end
    end
  end

  def test_mark_dry_run_writes_nothing
    with_fake_tc do
      with_stubbed_externals do
        a, _ = chain("a", "b")
        run_cli("-s", "a", "-q")
        _, out = run_cli("--mark-auto", "a", "-d", "-q")
        assert_match(/\[DRY RUN\] Mark a:1.0.0/, out)
        assert_equal :manual, mark_of(a)
      end
    end
  end

  def test_mark_takes_the_arch_modifier
    with_fake_tc do
      with_stubbed_externals do
        t = FakePackage.new("t")
        pkgmgr.register(t)
        with_context(ARCH: I386, BOARD: nil) { pkgmgr.install("t") }
        with_context(ARCH: RV, BOARD: nil) { pkgmgr.install("t") }

        with_context(ARCH: I386, BOARD: nil) do
          rc, _ = run_cli("--mark-auto", "t", "-a", "riscv64", "-q")
          assert_equal 0, rc
        end

        by_machine = marks(t).to_h { |(_, c), m| [c.machine, m] }
        assert_equal({ "tilck-i386" => :manual, "tilck-riscv64" => :auto },
                     by_machine)
      end
    end
  end

  def test_mark_takes_the_stack_modifier
    with_fake_tc do
      with_stubbed_externals do
        a, b = Ver("14.4.0"), Ver("16.2.0")
        s = FakePackage.new("host_s", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(s)
        pkgmgr.with_host_stack(a) { pkgmgr.install("host_s") }
        pkgmgr.with_host_stack(b) { pkgmgr.install("host_s") }

        rc, _ = run_cli("--mark-auto", "host_s", "-c", b.to_s, "-q")
        assert_equal 0, rc

        by_stack = marks(s).to_h { |(_, c), m| [c.stack, m] }
        assert_equal({ "gcc-#{a}" => :manual, "gcc-#{b}" => :auto },
                     by_stack)
      end
    end
  end

  # ALL is -u's ALL: the cross compilers stay out of it unless -f.
  def test_mark_all_leaves_the_compilers_alone_unless_forced
    with_fake_tc do
      with_stubbed_externals do
        # Both ways of being a compiler: one carries the target
        # metadata GCC's installs carry, one merely declares it.
        t = FakePackage.new("t")
        cc = FakePackage.new("gcc-i386-musl", on_host: true,
                             is_compiler: true, host_tier: :portable,
                             arch_list: ALL_HOST_ARCHS.values,
                             target_arch: I386)
        bare = FakePackage.new("gcc-x86_64-musl", on_host: true,
                               is_compiler: true, host_tier: :portable,
                               arch_list: ALL_HOST_ARCHS.values)
        [t, cc, bare].each { |p| pkgmgr.register(p); pkgmgr.install(p.name) }

        run_cli("--mark-auto", "ALL", "-q")
        assert_equal :auto, mark_of(t)
        assert_equal :manual, mark_of(cc)
        assert_equal :manual, mark_of(bare)

        run_cli("--mark-auto", "ALL", "-f", "-q")
        assert_equal :auto, mark_of(cc)
        assert_equal :auto, mark_of(bare)
      end
    end
  end

  # What -u never removes, no mark reaches: the ruby the package
  # manager itself runs on is nobody's to hand to --autoremove.
  def test_what_is_never_removed_is_never_marked
    with_fake_tc do
      with_stubbed_externals do
        ruby = FakePackage.new("ruby")
        pkgmgr.register(ruby)
        pkgmgr.install("ruby")

        _, out = run_cli("--mark-auto", "ruby", "-q")
        assert_match(/nothing matched/, out)
        run_cli("--mark-auto", "ALL", "-f", "-q")
        assert_equal :manual, mark_of(ruby)
      end
    end
  end

  def test_marking_what_is_not_there_says_so
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a"))
        rc, out = run_cli("--mark-auto", "a", "-q")
        assert_equal 0, rc
        assert_match(/nothing matched, so nothing was marked/, out)
      end
    end
  end

  # --- --autoremove ---------------------------------------------------

  # a -> b, c -> d; a is the user's, c is not: c and d go, b stays
  # because a needs it, and c goes before d.
  def autoremove_world
    a, b = chain("a", "b")
    # d registered before c, so that registration order is not the
    # order the removal must find on its own.
    d = FakePackage.new("d")
    c = FakePackage.new("c", dep_list: [Dep("d", false)])
    pkgmgr.register(d)
    pkgmgr.register(c)
    run_cli("-s", "a", "-q")
    run_cli("-s", "c", "-q")
    run_cli("--mark-auto", "c", "-q")
    return [a, b, c, d]
  end

  def test_autoremove_takes_the_auto_installs_nothing_kept_needs
    with_fake_tc do
      with_stubbed_externals do
        a, b, c, d = autoremove_world
        rc, out = run_cli("--autoremove", "-q")
        assert_equal 0, rc

        assert_equal 1, marks(a).length
        assert_equal 1, marks(b).length, "b is needed by a"
        assert_empty marks(c)
        assert_empty marks(d)

        gone = out.scan(/Remove pkg '(\w+)'/).flatten
        assert_equal %w[c d], gone, "dependents first"
        assert_match(/Removed: 2 installation/, out)
      end
    end
  end

  def test_autoremove_dry_run_removes_nothing
    with_fake_tc do
      with_stubbed_externals do
        a, b, c, d = autoremove_world
        rc, out = run_cli("--autoremove", "-d", "-q")
        assert_equal 0, rc
        assert_match(/\[DRY RUN\] Remove pkg 'c'/, out)
        assert_match(/Would remove: 2 installation/, out)
        assert_equal [1, 1, 1, 1], [a, b, c, d].map { |p| marks(p).length }
      end
    end
  end

  # A target install needs the cross compiler that built it, and a
  # host install does not: the i386 compiler stays for t, the x86_64
  # one goes.
  def test_autoremove_keeps_the_cross_compiler_a_target_install_needs
    with_fake_tc do
      with_stubbed_externals do
        t = FakePackage.new("t")
        p = FakePackage.new("host_p", on_host: true, host_tier: :portable,
                            arch_list: ALL_HOST_ARCHS.values)
        cc = { I386 => "gcc-i386-musl", X64 => "gcc-x86_64-musl" }.map {
          |arch, name|
          FakePackage.new(name, on_host: true, is_compiler: true,
                          host_tier: :portable,
                          arch_list: ALL_HOST_ARCHS.values, target_arch: arch)
        }
        ([t, p] + cc).each { |x| pkgmgr.register(x) }
        with_context(ARCH: I386, BOARD: nil) do
          pkgmgr.install("t")
          pkgmgr.install("host_p")
          cc.each { |x| pkgmgr.install(x.name, manual: false) }

          rc, _ = run_cli("--autoremove", "-q")
          assert_equal 0, rc
        end

        assert_equal 1, marks(cc[0]).length, "t needs its compiler"
        assert_empty marks(cc[1]), "nothing here needs the x86_64 one"
      end
    end
  end

  # The stack compiler lives in the distro's env and needs a package
  # in the stack it defines. Asked at the stack in effect instead, the
  # 11.5.0 compiler needed 14.4.0's glibc, and --autoremove offered to
  # take the glibc of every stack but the current one.
  def test_autoremove_asks_a_stack_compiler_at_its_own_stack
    with_fake_tc do
      with_stubbed_externals do
        a, b = Ver("11.5.0"), Ver("14.4.0")
        libc = FakePackage.new("host_libc", on_host: true, host_tier: :stack,
                               arch_list: ALL_HOST_ARCHS.values)
        gcc = FakePackage.new("host_gcc", on_host: true, host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values,
                              dep_list: [Dep("host_libc", true)])
        gcc.define_singleton_method(:installable_versions) { [a, b] }
        gcc.define_singleton_method(:stack_gcc_ver) { |v = nil|
          v || pkgmgr.current_host_stack
        }
        gcc.define_singleton_method(:stack_of_install) { |i| i.ver }
        pkgmgr.register(libc)
        pkgmgr.register(gcc)

        # gcc A, asked for by name, with its libc in stack A; a libc in
        # stack B that nothing needs. The stack in effect is B.
        pkgmgr.with_host_stack(a) {
          pkgmgr.install("host_libc", manual: false)
          pkgmgr.install("host_gcc", a)
        }
        pkgmgr.with_host_stack(b) { pkgmgr.install("host_libc", manual: false) }
        pkgmgr.host_stack = b

        rc, out = run_cli("--autoremove", "-q")
        assert_equal 0, rc
        gone = out.scan(/Remove pkg 'host_libc' install at (\S+)/).flatten
        assert_equal 1, gone.length, out
        assert_includes gone.first, "gcc-#{b}", "stack B's libc goes"
        assert_equal 1, libc.get_install_list.length, "stack A's libc stays"
      end
    end
  end

  def test_autoremove_with_nothing_to_take_says_so
    with_fake_tc do
      with_stubbed_externals do
        chain("a", "b")
        run_cli("-s", "a", "-q")
        rc, out = run_cli("--autoremove", "-q")
        assert_equal 0, rc
        assert_match(/Nothing to remove/, out)
      end
    end
  end

  # --- the mark survives what changes an install ----------------------

  def test_upgrade_keeps_the_mark_of_the_install_it_replaces
    with_fake_tc do
      with_stubbed_externals do
        auto = FakePackage.new("auto")
        manual = FakePackage.new("manual")
        pkgmgr.register(auto)
        pkgmgr.register(manual)
        fake_install(auto, "0.9.0", mark: :auto)
        fake_install(manual, "0.9.0", mark: :manual)

        rc, _ = run_cli("--upgrade", "-q")
        assert_equal 0, rc
        assert_equal :auto, mark_of(auto)
        assert_equal :manual, mark_of(manual)
      end
    end
  end

  # The default install claims the default set, and upgrades what is
  # beside it the way --upgrade does.
  def test_the_default_install_claims_the_defaults_and_upgrades_the_rest
    with_fake_tc do
      with_stubbed_externals do
        dflt = FakePackage.new("dflt", default: true)
        multi = FakePackage.new("multi")
        pkgmgr.register(dflt)
        pkgmgr.register(multi)
        fake_install(dflt, mark: :auto)
        fake_install(multi, "0.9.0", mark: :auto)

        rc, out = run_cli("-q")
        assert_equal 0, rc
        assert_match(/Set dflt:1.0.0 to manually installed/, out)
        assert_equal :manual, mark_of(dflt)
        assert_equal :auto, mark_of(multi)
      end
    end
  end

  def test_rebuild_keeps_the_mark_and_brings_dependencies_in_as_auto
    with_fake_tc do
      with_stubbed_externals do
        a, b = chain("a", "b")
        fake_install(a, record: :changed, mark: :auto)

        rc, _ = run_cli("--rebuild", "-q")
        assert_equal 0, rc
        assert_equal :auto, mark_of(a)
        assert_equal :auto, mark_of(b)
      end
    end
  end

  # --- the record -----------------------------------------------------

  def test_a_one_word_record_reads_as_manual
    with_fake_tc do
      with_stubbed_externals do
        a = FakePackage.new("a")
        pkgmgr.register(a)
        pkgmgr.install("a")
        inst = a.get_install_list.find { |i| !i.path.nil? }
        File.write(inst.path / InstallOrigin::FILE, "pinned\n")

        pkgmgr.installs_changed!
        pkgmgr.refresh
        inst = a.get_install_list.find { |i| !i.path.nil? }
        assert inst.manual
        refute inst.default_install
      end
    end
  end
end
