# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'busybox'  # for BUSYBOX_SOURCE

#
# host_menuconfig: the Linux-kernel-style `mconf` and `conf` binaries,
# used by Tilck's new menuconfig-style configurator.
#
# The source IS Busybox. Busybox ships a standalone-buildable copy of
# Linux's scripts/kconfig/ in its tarball — the same source that
# busybox's own `-C busybox` menuconfig flow compiles. We share
# BUSYBOX_SOURCE so the tarball is fetched once and extracted twice
# (Linux kernel's SourceRef pattern from gnuefi).
#
# Busybox 1.36.1 does NOT ship nconf.c; scope for this first PR is
# mconf (the classic dialog-based UI) and conf (non-interactive,
# used for round-trip tests). Adding nconf can come from a richer
# source (u-boot, or a direct torvalds/linux partial clone) in a
# later PR.
#
# host_ncurses provides libncursesw.a + libtinfo.a via its
# HOSTCFLAGS / HOSTLDFLAGS helper (see Package#host_ncurses_build_flags).
#
class HostMenuconfigPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_menuconfig',
      source: BUSYBOX_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_ncurses', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  # BUSYBOX_SOURCE's pkg_versions entry is VER_BUSYBOX, not
  # VER_MENUCONFIG (which doesn't exist). Override default_ver so the
  # shared tarball is located correctly in the cache.
  def default_ver = pkgmgr.get_config_ver("busybox")

  def expected_files = [
    ["install/bin/mconf", false],
    ["install/bin/conf",  false],
    ["install/bin/lxdialog", false],
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def install_impl_internal(install_dir)

    make_vars, _env = host_ncurses_build_flags

    # Seed .config from the one busybox.rb uses for its own build —
    # content doesn't matter, we just need a file so silentoldconfig
    # has something to read (otherwise conf errors out with
    # "You have not yet configured busybox").
    cp MAIN_DIR / "other" / "busybox.config", ".config"

    # silentoldconfig: compiles `conf` and runs `conf -s Config.in`,
    # which exits cleanly with a valid .config present.
    ok = run_command("silentoldconfig.log", [
      "make", *make_vars,
      "-j#{BUILD_PAR}",
      "silentoldconfig",
    ])
    return false if !ok

    # menuconfig: compiles `mconf` + lxdialog, then tries to run
    # mconf. Without a TTY, mconf prints a message and exits 0
    # cleanly ("configuration changes were NOT saved"), so we don't
    # need any redirection / tolerant-exit handling.
    ok = run_command("menuconfig.log", [
      "make", *make_vars,
      "-j#{BUILD_PAR}",
      "menuconfig",
    ])
    return false if !ok

    # Hand-install: Busybox's Makefile doesn't expose an install
    # target for just the kconfig tools, so copy them directly.
    bin_dir = mkpathname("#{install_dir}/install/bin")
    FileUtils.mkdir_p(bin_dir)
    for bin in %w[mconf conf]
      FileUtils.cp("scripts/kconfig/#{bin}", "#{bin_dir}/")
    end
    # mconf shells out to lxdialog at runtime for dialog boxes —
    # it lives in scripts/kconfig/lxdialog/ and must be on PATH
    # (or next to mconf) when mconf runs.
    FileUtils.cp("scripts/kconfig/lxdialog/lxdialog", "#{bin_dir}/")
    return true
  end
end

pkgmgr.register(HostMenuconfigPackage.new())
