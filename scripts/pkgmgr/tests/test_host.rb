# SPDX-License-Identifier: BSD-2-Clause
#
# THE HOST AS A VALUE, and placement reading it from the scope.
#
# Where a host package lives -- the <machine> of everything built to
# run here, the distro <env>, the compiler <stack> -- was read from
# the HOST_* constants wherever it was decided, so a question about
# another host could not even be asked: the model and the lane
# enumerate (arch, board) and had no host to vary. Now the host is a
# value in the scope (Scope#host, Host.env at the boundary), the tree
# reader takes the host it scans as an argument, and this file asks
# the questions from a host that is not this one.
#

require_relative 'test_helper'
require_relative '../host'
require_relative '../world'
require_relative '../portability'

class TestHostValue < Minitest::Test

  include TestHelper

  ELSEWHERE = Host.new(os: "linux", arch: ALL_ARCHS["aarch64"],
                       distro: "debian-12", cc: "gcc-12.2.0")

  def setup = reset_pkgmgr!

  def test_the_machine_is_the_os_and_the_arch
    assert_equal "linux-aarch64", ELSEWHERE.machine
    assert_equal "linux-aarch64 debian-12 gcc-12.2.0", ELSEWHERE.to_s
    h = Host.env
    assert_equal HOST_OS_ARCH, h.machine
    assert_equal [HOST_DISTRO, HOST_CC], [h.distro, h.cc]
  end

  def test_the_environment_scope_carries_this_host
    assert_equal Host.env, scope.host
    assert_equal Host.env, scope.with(arch: ALL_ARCHS["riscv64"]).host,
                 "with() keeps the host"
  end

  # Each host tier's coordinates follow the scope's host, not the
  # process's.
  def test_placement_follows_the_scopes_host
    away = scope.with(arch: scope.arch)
    away = Scope.new(**away.to_h.merge(host: ELSEWHERE))
    tiers = { portable: "linux-aarch64/any/any",
              distro:   "linux-aarch64/debian-12/any",
              compiler: "linux-aarch64/debian-12/gcc-12.2.0" }
    for tier, want in tiers do
      pkg = FakePackage.new("host_#{tier}", on_host: true, host_tier: tier,
                            arch_list: ALL_HOST_ARCHS.values)
      assert_equal want, pkg.at(away).coords.to_s, tier.to_s
      assert_equal ALL_ARCHS["aarch64"], pkg.at(away).default_arch
    end
    assert_equal "#{HOST_OS_ARCH}/#{HOST_DISTRO}/any",
                 FakePackage.new("host_here", on_host: true,
                                 host_tier: :distro).at(scope).coords.to_s
  end

  def test_a_stack_package_follows_the_scopes_host_too
    away = Scope.new(**scope.to_h.merge(host: ELSEWHERE))
    pkg = FakePackage.new("host_s", on_host: true, host_tier: :stack,
                          arch_list: ALL_HOST_ARCHS.values)
    assert_equal "linux-aarch64/any/gcc-#{scope.stack}",
                 pkg.at(away).coords.to_s
  end

  # Whether a package may run is asked of a host: the scope's, or the
  # one named.
  def test_host_support_is_asked_of_a_host
    mac_only = FakePackage.new("host_m", on_host: true,
                               host_os_list: ["macos"])
    mac = Host.new(os: "macos", arch: ALL_ARCHS["aarch64"],
                   distro: "macos-14", cc: "clang-15.0.0")
    assert mac_only.own_host_supported?(mac)
    refute mac_only.own_host_supported?(ELSEWHERE)
    refute mac_only.at(scope).own_host_supported?, "this host is linux"
    assert mac_only.at(Scope.new(**scope.to_h.merge(host: mac)))
                   .host_supported?
  end

  # The tree reader scans the host it is given: a tree that holds
  # another host's packages answers about that host, and this host's
  # reading does not see them.
  def test_the_scan_reads_the_host_it_is_given
    with_fake_tc do
      pkg = FakePackage.new("host_thing", on_host: true, host_tier: :distro,
                            arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)
      # Not fake_install: it holds an install to THIS host's reading,
      # which is exactly what an install elsewhere must escape.
      there = Coords.new("linux-aarch64", "debian-12", nil)
      dir = bound(pkg).pkg_dir_at(there) / "1.0.0"
      FileUtils.mkdir_p(dir)
      InstallOrigin.write(dir, true, true)

      here = World.scan(pkgmgr.all_packages, host: Host.env)
      assert_empty here.of("host_thing"), "another host's install read here"
      assert_empty here.orphans, "or taken for an orphan"

      away = World.scan(pkgmgr.all_packages, host: ELSEWHERE)
      assert_equal [there], away.of("host_thing").map(&:coords)
      assert_equal [ALL_ARCHS["aarch64"]], away.of("host_thing").map(&:arch)
    end
  end
end

# The facts of a host's binaries, one table keyed by machine. Five
# x86_64-glibc constants lived in four files; the stack cannot be
# brought up elsewhere without finding them all, and now they are one
# row.
class TestHostABI < Minitest::Test

  include TestHelper

  def test_this_host_has_a_row_and_the_scope_reads_it
    abi = Host.env.abi
    refute_nil abi, "no ABI row for #{Host.env.machine}"
    assert_equal Host.env.machine, abi.machine
    assert_same abi, scope.host.abi
  end

  def test_the_x86_64_row_is_what_the_constants_said
    abi = HostABI.for("linux-x86_64")
    assert_equal 62, abi.elf_machine
    assert_equal "usr/lib/ld-linux-x86-64.so.2", abi.loader
    assert_equal "/lib64/ld-linux-x86-64.so.2", abi.system_loader
    assert_equal "lib64", abi.gcc_libdir
    assert_includes abi.libdirs, "/usr/lib/x86_64-linux-gnu"
    assert_includes abi.libdirs, "/lib64"
  end

  def test_a_host_the_table_does_not_know_has_no_abi
    away = Host.new(os: "linux", arch: ALL_ARCHS["aarch64"],
                    distro: "debian-12", cc: "gcc-12.2.0")
    assert_nil away.abi
    assert_nil HostABI.for("darwin-aarch64")
  end

  # The ELF filter is the row's, not a constant: a row with another
  # machine accepts that machine's binaries and rejects this one's.
  def test_the_elf_filter_follows_the_row
    arm = HostABI.new(machine: "linux-aarch64", elf_machine: 183,
                      loader: "usr/lib/ld-linux-aarch64.so.1",
                      system_loader: "/lib/ld-linux-aarch64.so.1",
                      libdirs: ["/usr/lib/aarch64-linux-gnu", "/usr/lib"],
                      gcc_libdir: "lib")
    Dir.mktmpdir do |d|
      hdr = ->(em) { "\x7fELF".b + [2, 1, 1].pack("C3") + ("\0" * 9) +
                     [2].pack("v") + [em].pack("v") + ("\0" * 44) }
      x86 = File.join(d, "x86"); File.binwrite(x86, hdr.call(62))
      a64 = File.join(d, "a64"); File.binwrite(a64, hdr.call(183))
      assert Portability.elf?(x86, abi: HostABI.for("linux-x86_64"))
      refute Portability.elf?(a64, abi: HostABI.for("linux-x86_64"))
      assert Portability.elf?(a64, abi: arm)
      refute Portability.elf?(x86, abi: arm)
    end
  end
end
