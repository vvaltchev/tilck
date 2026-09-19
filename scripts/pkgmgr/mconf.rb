# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'busybox'  # for BUSYBOX_SOURCE

#
# host_mconf: the Linux-kernel-style `mconf`, `conf` and `lxdialog`
# binaries, used by Tilck's menuconfig-style configurator.
#
# Named after what it contains, not after where the source comes from.
# The source IS Busybox: it ships a standalone-buildable copy of
# Linux's scripts/kconfig/ in its tarball — the same source that
# busybox's own `-C busybox` menuconfig flow compiles. We share
# BUSYBOX_SOURCE so one tarball can serve both packages, but the two
# versions are independent: this package's is HOST_VER_MCONF in
# other/host_pkg_versions, spelled as the busybox release the tools
# are cut from, and the target busybox has its own VER_BUSYBOX. When
# they happen to coincide, the cache holds a single tarball.
#
# Busybox 1.36.1 does NOT ship nconf.c; scope for this first PR is
# mconf (the classic dialog-based UI) and conf (non-interactive,
# used for round-trip tests). Adding nconf can come from a richer
# source (u-boot, or a direct torvalds/linux partial clone) in a
# later PR.
#
# host_ncurses provides libncursesw.a + libtinfo.a through the build
# interface it publishes (see NcursesHostPackage#build_env).
#
class HostMconfPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_mconf',
      source: BUSYBOX_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_ncurses', true)],
      default: false,
    )
  end

  def expected_files(ver = nil) = [
    ["install/bin/mconf", false],
    ["install/bin/conf",  false],
    ["install/bin/lxdialog", false],
  ]

  def build_steps(ver = default_ver)

    # What host_ncurses publishes -- include and lib dirs, its
    # pkg-config dir -- in tokens the runner resolves. env({}) rather
    # than env: the recipe must not read this process's environment,
    # and the inherited PKG_CONFIG_PATH it would have appended is what
    # a system ncurses gets found through, which is the one thing this
    # package exists to avoid.
    be = deps_build_env

    return [

      # The busybox tarball ships an `INSTALL` file at the top level.
      # On case-insensitive filesystems (e.g. APFS on macOS) this
      # collides with the `install/` prefix directory created below
      # to hold the packaged binaries, so `mkdir_p install/bin` fails
      # with EEXIST. The file is user documentation -- nothing in the
      # build references it -- so drop it to free the name. Remove
      # never minds a file that is not there.
      Remove(paths: ["INSTALL"]),

      # Seed .config from the one busybox.rb uses for its own build.
      # Its content does not matter; silentoldconfig just needs a
      # file to read, or conf errors out with "You have not yet
      # configured busybox".
      Copy(from: src_path(MAIN_DIR / "other" / "busybox.config"),
           to: ".config"),

      # silentoldconfig: compiles `conf` and runs `conf -s Config.in`,
      # which exits cleanly with a valid .config present.
      Run(log: "silentoldconfig.log", argv: [
        "make", *be.kconfig_make_vars, "-j$PAR", "silentoldconfig",
      ]),

      # busybox's scripts/kconfig/lxdialog/check-lxdialog.sh discovers
      # ncurses via `pkg-config --libs ncursesw`; PKG_CONFIG_PATH makes
      # that resolve to our pinned host_ncurses, not the system one --
      # the whole reason host_ncurses exists is to stop relying on a
      # system ncurses-dev, which is unreliable on macOS (no
      # libncursesw, so -lncursesw falls through to a system
      # libncurses with no curses.h or wide-char headers).
      #
      # menuconfig: compiles `mconf` + lxdialog, then tries to RUN
      # mconf. Only the binaries are wanted; the run is an unwanted
      # side effect that would hang waiting for input on a TTY. With
      # TERM empty, ncurses' initscr() errors out immediately ("Error
      # opening terminal: ."), lxdialog exits nonzero, mconf treats
      # that as "quit without saving" and exits 0 -- and the built
      # mconf + lxdialog are on disk, ready to copy.
      Within(env: be.env({}).merge("TERM" => ""), steps: [
        Run(log: "menuconfig.log", argv: [
          "make", *be.kconfig_make_vars, "-j$PAR", "menuconfig",
        ]),
      ]),

      # Hand-install: busybox's Makefile has no install target for
      # just the kconfig tools. mconf shells out to lxdialog at
      # runtime for its dialog boxes, so that goes beside it.
      Mkdir(path: "$INSTALL/install/bin"),
      Copy(from: "scripts/kconfig/mconf", to: "$INSTALL/install/bin/"),
      Copy(from: "scripts/kconfig/conf", to: "$INSTALL/install/bin/"),
      Copy(from: "scripts/kconfig/lxdialog/lxdialog",
           to: "$INSTALL/install/bin/"),

      # The deliverable is those three binaries: ~600 KB against
      # ~22 MB of busybox source of no further use.
      Prune(),
    ]
  end
end

pkgmgr.register(HostMconfPackage.new())
