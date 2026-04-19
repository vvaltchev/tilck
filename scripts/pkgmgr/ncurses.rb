# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# Shared SourceRef: NcursesPackage (target, cross-compiled for Tilck)
# and NcursesHostPackage (host, native build used by busybox and u-boot
# menuconfig) both fetch from the same upstream — the tarball is
# downloaded once and cached.
#
NCURSES_SOURCE = SourceRef.new(
  name: 'ncurses',
  url:  'https://ftp.gnu.org/pub/gnu/ncurses',
  tarname: ->(ver) { "ncurses-#{ver}.tar.gz" },
)

# ncurses 6.5's configure uses an old autoconf idiom that *unsets* the
# locale variables instead of forcing them to C:
#
#   $as_unset LANG || test "${LANG+set}" != set || { LANG=C; export LANG; }
#
# The `||` chain means: try `unset LANG`; if it works, stop. So on any
# normal shell every locale var ends up unset for the rest of configure.
# On macOS, Homebrew's gawk 5.4.0 has a bug where, with *no* locale at all
# (not even C), `gsub("[+]", " ", s)` silently fails to replace anything.
# That breaks mk-1st.awk's in_subset() helper, so the per-model rules
# never get appended to ncurses/Makefile, and `make` then dies with
# "No rule to make target ../lib/libncurses.a".
#
# Patch the buggy idiom to unconditionally export the locale vars to C.
def ncurses_patch_configure_locale
  data = File.read("configure")
  data.gsub!(
    /^\$as_unset (\w+) \|\| test "\$\{\1\+set\}" != set \|\| \{ \1=C; export \1; \}$/,
    'export \1=C'
  )
  File.write("configure", data)
end

class NcursesPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'ncurses',
      source: NCURSES_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS.values,
      dep_list: []
    )
  end

  def expected_files = [
    ["install/lib/libncurses.a", false]
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def install_impl_internal(install_dir)

    arch = default_arch().gcc_tc

    # The ncurses tarball ships an `INSTALL` documentation file that, on
    # case-insensitive filesystems (e.g. APFS on macOS), collides with the
    # `install/` prefix directory `make install` wants to create. The file
    # is documentation only — no Makefile target depends on it — so just
    # remove it to make `mkdir install` work.
    File.delete("INSTALL") if File.exist?("INSTALL")

    ncurses_patch_configure_locale

    # Pass --build so configure sets cross_compiling=yes immediately
    # (when both --host and --build are set and differ).  Without it,
    # configure sets cross_compiling=maybe and tries to run a cross-
    # compiled test binary, which on FreeBSD triggers a kernel
    # uprintf "ELF binary type '0' not known." to the terminal.
    #
    # BUILD_CC tells ncurses which compiler to use for host-side
    # build tools; without it BUILD_CC defaults to $CC (the cross-
    # compiler) causing the same exec-of-Linux-binary problem.
    host_cc = "cc"
    build_triple = "#{HOST_ARCH.name}-unknown-#{HOST_OS}"

    ok = run_command("configure.log", [
      "./configure",
      "--host=#{arch}-pc-linux-gnu",
      "--build=#{build_triple}",
      "--prefix=#{install_dir}/install",
      "--datarootdir=/usr/share",
      "--disable-db-install",
      "--disable-widec",
      "--without-progs",
      "--without-cxx",
      "--without-cxx-binding",
      "--without-ada",
      "--without-manpages",
      "--without-dlsym",
      "BUILD_CC=#{host_cc}",
    ])
    return false if !ok

    ok = run_command("build.log", [ "make", "-j#{BUILD_PAR}" ])
    return false if !ok

    ok = run_command("install.log", [ "make", "install" ])
    return ok
  end
end

#
# Host-native ncurses build. Shares NCURSES_SOURCE with the target
# build above — the tarball is fetched once and extracted twice (once
# for the target cross-compile, once for the host-native compile).
#
# Used by busybox's and u-boot's `-C` (menuconfig) flows so that the
# kconfig host tools (mconf, nconf) link against a pinned ncurses that
# is consistent across host distributions, instead of relying on
# libncurses-dev being installed on the host (fine on Debian/Fedora/
# Arch/FreeBSD, but unreliable on macOS where the system ncurses is
# old and keg-only via Homebrew).
#
class NcursesHostPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_ncurses',
      source: NCURSES_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [],
      default: true,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  # Built with --enable-widec because busybox's and u-boot's kconfig
  # check-lxdialog.sh prefers -lncursesw over -lncurses. Without the
  # wide-char library in our install tree, the linker falls through
  # to the system libncursesw — the exact failure mode this package
  # is supposed to prevent. --with-termlib=tinfo splits terminfo into
  # a separately-named libtinfo (not libtinfow) so -ltinfo in the
  # kconfig link line also resolves locally.
  #
  # ncurses installs headers under include/ncursesw/ with widec (curses.h,
  # term.h, etc.). Consumers that use pkg-config get the right -I flag
  # automatically from ncursesw.pc; others need the explicit include
  # paths surfaced by Package#host_ncurses_build_flags.
  def expected_files = [
    ["install/lib/libncursesw.a", false],
    ["install/lib/libtinfo.a", false],
    ["install/include/ncursesw/curses.h", false],
    ["install/lib/pkgconfig/ncursesw.pc", false],
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def install_impl_internal(install_dir)

    # Same case-insensitive-FS workaround as the target build.
    File.delete("INSTALL") if File.exist?("INSTALL")

    ncurses_patch_configure_locale

    # At runtime, ncurses needs to find terminfo entries for the
    # user's $TERM. The system ncurses on each host distro has its
    # own set of search paths compiled in; ours used to only know
    # about --datarootdir=/usr/share, which missed tmux-256color on
    # Ubuntu (the entry lives at /lib/terminfo/t/tmux-256color, not
    # /usr/share/terminfo).
    #
    # Bake in every standard terminfo location across Linux / FreeBSD
    # / macOS so `initscr()` resolves $TERM regardless of where the
    # distro puts its database. Non-existent dirs are silently
    # skipped at lookup time.
    #
    #   Linux   : /etc/terminfo, /lib/terminfo, /usr/share/terminfo
    #   FreeBSD : /usr/share/misc/terminfo (base),
    #             /usr/local/share/terminfo (ports),
    #             /usr/local/share/site-terminfo (per-port)
    #   Darwin  : /usr/share/terminfo (old, usually missing newer
    #             entries), /opt/homebrew/share/terminfo (AS),
    #             /opt/homebrew/opt/ncurses/share/terminfo (keg-only),
    #             /usr/local/share/terminfo (Intel Homebrew)
    terminfo_dirs = [
      "/etc/terminfo",
      "/lib/terminfo",
      "/usr/share/terminfo",
      "/usr/share/misc/terminfo",
      "/usr/local/share/terminfo",
      "/usr/local/share/site-terminfo",
      "/opt/homebrew/share/terminfo",
      "/opt/homebrew/opt/ncurses/share/terminfo",
    ].join(":")

    ok = run_command("configure.log", [
      "./configure",
      "--prefix=#{install_dir}/install",
      "--datarootdir=/usr/share",
      "--disable-db-install",
      "--with-terminfo-dirs=#{terminfo_dirs}",
      # Wide-char ncurses: busybox/u-boot's kconfig prefers -lncursesw.
      "--enable-widec",
      # Split terminfo into a separately-named library so the kconfig
      # link line's `-ltinfo` finds ours, not the system's. Without
      # the =tinfo suffix the lib would be named libtinfow (widec
      # default), which would NOT match `-ltinfo`.
      "--with-termlib=tinfo",
      # --enable-pc-files opts in to installing pkg-config files (OFF
      # by default upstream); --with-pkg-config-libdir directs them
      # into our tree so PKG_CONFIG_PATH can discover them without
      # touching the system pkg-config search path.
      "--enable-pc-files",
      "--with-pkg-config-libdir=#{install_dir}/install/lib/pkgconfig",
      "--without-progs",
      "--without-cxx",
      "--without-cxx-binding",
      "--without-ada",
      "--without-manpages",
    ])
    return false if !ok

    ok = run_command("build.log", [ "make", "-j#{BUILD_PAR}" ])
    return false if !ok

    ok = run_command("install.log", [ "make", "install" ])
    return false if !ok

    # Post-install fixup: ncurses bakes the `--prefix` path (which is
    # the *staging* dir at configure time) into .pc files and
    # ncurses6-config. After install_impl atomically mv's staging to
    # the final host toolchain location, those baked paths would be
    # dangling. Rewrite them to use paths that resolve at query time
    # from the script/pc-file's own location, so the files survive
    # relocation.
    ncurses_host_fix_baked_paths("#{install_dir}/install")
    return true
  end
end

# Rewrite hardcoded prefix paths to relocatable equivalents in the
# host_ncurses install tree. Called AFTER `make install` so the files
# survive the atomic staging→final mv. See NcursesHostPackage.
def ncurses_host_fix_baked_paths(prefix_dir)
  # pkg-config files: make prefix= pcfiledir-relative. Every .pc sits
  # at <prefix>/lib/pkgconfig/, so going two levels up lands at <prefix>.
  Dir.glob("#{prefix_dir}/lib/pkgconfig/*.pc").each do |pc|
    data = File.read(pc)
    m = data.match(/^prefix=(.+)$/)
    next if !m
    staged = m[1]
    data.sub!(/^prefix=.+$/, 'prefix=${pcfiledir}/../..')
    data.gsub!(staged, '${prefix}')
    File.write(pc, data)
  end

  # ncurses{,w}6-config is a POSIX shell script with the prefix baked
  # in. Replace the static prefix="..." assignment with one that
  # derives the prefix from $0's directory at runtime
  # (<prefix>/bin/ -> <prefix>). The script is named ncurses6-config
  # without widec and ncursesw6-config with --enable-widec.
  Dir.glob("#{prefix_dir}/bin/ncurses*6-config").each do |script|
    data = File.read(script)
    m = data.match(/^prefix="([^"]*)"/)
    next if !m
    staged = m[1]
    data.sub!(
      /^prefix="[^"]*"/,
      'prefix="$(cd -- "$(dirname -- "$0")/.." && pwd)"'
    )
    data.gsub!(staged, '${prefix}')
    File.write(script, data)
  end
end

pkgmgr.register(NcursesPackage.new())
pkgmgr.register(NcursesHostPackage.new())
