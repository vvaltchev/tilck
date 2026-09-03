# SPDX-License-Identifier: BSD-2-Clause
#
# THE HOST WORLD RUNS WHERE ITS ROOTS RUN.
#
# host_gcc and host_qemu are the roots of the host world: our own GCC
# with its glibc sysroot, the QEMU matrix built by it, and the fifty
# packages nothing else needs. That world is a Linux userland by
# construction -- kernel headers, a glibc, a compiler targeting them
# -- and it has been built and exercised on x86_64 only. The roots say
# so; everything only they need follows by derivation, so that fifty
# packages are hidden by two declarations and a rule, not by fifty
# flags.
#

require_relative 'test_helper'
require_relative '../main'

class TestHostWorld < Minitest::Test

  include TestHelper

  # Snapshotted at load: the registry is reset per test elsewhere.
  PACKAGES = pkgmgr.all_packages.dup.freeze

  def setup
    reset_pkgmgr!
    PACKAGES.each { |p| pkgmgr.register(p) }
  end

  def world = pkgmgr.host_world_names
  def pkg(name) = pkgmgr.get(name)

  # --- the real registry ----------------------------------------------------

  def test_the_roots_say_where_the_world_runs
    for r in pkgmgr.host_world_roots do
      assert_equal ["linux"], r.host_os_list, r.name
      assert_equal ["x86_64"], r.host_arch_list, r.name
    end
    assert_equal %w[host_gcc host_qemu],
                 pkgmgr.host_world_roots.map(&:name).sort
  end

  # This suite runs on x86_64 Linux, where the whole world is supported.
  def test_the_world_is_supported_on_x86_64_linux
    with_context(HOST_OS: "linux", HOST_ARCH: ALL_HOST_ARCHS["x86_64"]) do
      unsupported = world.reject { |n| pkg(n).host_supported? }
      assert_empty unsupported
    end
  end

  def test_the_world_is_hidden_on_macos
    with_context(HOST_OS: "macos") do
      still = world.select { |n| pkg(n).host_supported? }
      assert_empty still, "supported on macOS: #{still.join(' ')}"

      # ...and only the world is: what Tilck itself needs stays.
      assert pkg("host_ncurses").host_supported?
      assert pkg("host_mtools").host_supported?
      assert pkg("gcc-i386-musl").host_supported?
    end
  end

  def test_the_world_is_hidden_on_aarch64_linux
    with_context(HOST_OS: "linux", HOST_ARCH: ALL_HOST_ARCHS["aarch64"]) do
      still = world.select { |n| pkg(n).host_supported? }
      assert_empty still, "supported on aarch64: #{still.join(' ')}"
      assert pkg("host_ncurses").host_supported?
    end
  end

  # The fifty-three are hidden by derivation: none of them declares
  # an OS or an arch of its own (host_sophgo_tools, which does, is
  # outside the world).
  def test_only_the_roots_declare_it
    declared = world.reject { |n| pkg(n).host_world_root? }
                    .select { |n| pkg(n).host_os_list || pkg(n).host_arch_list }
    assert_empty declared
  end

  # --- what the user sees --------------------------------------------------

  def test_a_world_package_is_refused_at_the_door
    with_fake_tc do
      with_stubbed_externals do
        with_context(HOST_OS: "macos") do
          rc, out = run_cli("-s", "host_qemu", "-q")
          assert_equal 1, rc
          assert_match(/requires a linux x86_64 host/, out)
        end
      end
    end
  end

  def test_the_listing_omits_the_world_elsewhere
    with_fake_tc do
      with_stubbed_externals do
        with_context(HOST_OS: "macos") do
          _, out = run_cli("--list-installable", "-q")
          assert_match(/host_ncurses/, out)
          for n in world do
            refute_match(/^#{Regexp.escape(n)}\b/, out, "#{n} listed on macOS")
          end

          _, listing = run_cli("-l", "-q")
          refute_match(/host_qemu|host_gcc|host_gtk3/, listing)
        end
      end
    end
  end

  def test_there_are_no_stacks_elsewhere
    with_fake_tc do
      with_stubbed_externals do
        with_context(HOST_OS: "macos") do
          rc, out = run_cli("-L", "-q")
          assert_equal 0, rc
          assert_match(/No host stacks on macos-x86_64/, out)
        end
      end
    end
  end

  # --- the rule, on a world built by hand ----------------------------------

  class Root < TestHelper::FakePackage
    def initialize(name, **kw)
      super(name, on_host: true, host_tier: :distro,
            arch_list: ALL_HOST_ARCHS.values, **kw)
    end
    def host_world_root? = true
  end

  # A root that runs on Linux only; D only it needs; S needed by O too.
  # On macOS: the root and D are hidden, S and O are not.
  def test_what_only_the_roots_need_follows_them
    reset_pkgmgr!
    host = ->(n, **kw) {
      FakePackage.new(n, on_host: true, host_tier: :distro,
                      arch_list: ALL_HOST_ARCHS.values, **kw)
    }
    pkgmgr.register(Root.new("host_r", host_os_list: ["linux"],
                             dep_list: [Dep("host_d", true),
                                        Dep("host_s", true)]))
    pkgmgr.register(host.call("host_d"))
    pkgmgr.register(host.call("host_s"))
    pkgmgr.register(host.call("host_o", dep_list: [Dep("host_s", true)]))

    assert_equal %w[host_d host_r], pkgmgr.host_world_names.sort

    with_context(HOST_OS: "macos") do
      refute pkg("host_r").host_supported?
      refute pkg("host_d").host_supported?, "only the root needs it"
      assert pkg("host_s").host_supported?, "host_o needs it too"
      assert pkg("host_o").host_supported?
    end

    with_context(HOST_OS: "linux") do
      assert pkg("host_d").host_supported?
    end
  end
end
