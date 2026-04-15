# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

NCURSES_SOURCE = SourceRef.new(
  name: 'ncurses',
  url:  'https://ftp.gnu.org/pub/gnu/ncurses',
  tarname: ->(ver) { "ncurses-#{ver}.tar.gz" },
)

class NcursesPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'ncurses',
      source: NCURSES_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
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

    patch_configure_locale

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

  private

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
  def patch_configure_locale
    data = File.read("configure")
    data.gsub!(
      /^\$as_unset (\w+) \|\| test "\$\{\1\+set\}" != set \|\| \{ \1=C; export \1; \}$/,
      'export \1=C'
    )
    File.write("configure", data)
  end
end

pkgmgr.register(NcursesPackage.new())
