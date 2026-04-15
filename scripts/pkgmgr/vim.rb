# SPDX-License-Identifier: BSD-2-Clause

# This is the latest version of vim that works correctly. Version > v8.2.5056
# uses unsupported kernel features related to timers.
#
# See https://github.com/vim/vim/issues/10647

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

VIM_SOURCE = SourceRef.new(
  name: 'vim',
  url:  GITHUB + '/vim/vim',
)

class VimPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'vim',
      source: VIM_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: X86_ARCHS,
      dep_list: [Dep('ncurses', false)]
    )
  end

  def expected_files = [
    ["install/vim.gz", false],
    ["install/vr.tgz", false],
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def install_impl_internal(install_dir)

    arch = default_arch.gcc_tc
    ncurses_ver = pkgmgr.get_config_ver("ncurses")
    ncurses = ARCH.target_dir / "ncurses" / ncurses_ver.to_s / "install"

    with_saved_env(%w[CFLAGS LDFLAGS CPPFLAGS]) do

      ENV["CFLAGS"]   = "-ggdb -Os"
      ENV["LDFLAGS"]  = "-static -L#{ncurses}/lib -lncurses"
      ENV["CPPFLAGS"] = "-I#{ncurses}/include -I#{ncurses}/include/ncurses"

      configure_argv = [
        "./configure",
        "--prefix=#{install_dir}/install",
        "--build=#{HOST_ARCH.gcc_tc}-linux-gnu",
        "--host=#{arch}-linux-musl",
        "--target=#{arch}-linux-musl",
        "--with-features=normal",
        "--with-tlib=ncurses",
        "--without-x",
        "--enable-gui=no",
        "vim_cv_toupper_broken=no",
        "vim_cv_terminfo=yes",
        "vim_cv_tgetent=zero",
        "vim_cv_getcwd_broken=no",
        "vim_cv_stat_ignores_slash=no",
        "vim_cv_memmove_handles_overlap=yes",
      ]

      # macOS: vim's configure detects Darwin via `uname` and
      # unconditionally adds -DMACOS_X to CPPFLAGS (before even
      # checking --disable-darwin). This causes the cross-compiled
      # build to pull in macOS-specific code (os_macosx.m,
      # F_FULLFSYNC, mach/mach_host.h). Override the uname cache
      # var so configure takes the Linux path entirely.
      if OS == "Darwin"
        configure_argv += [
          "vim_cv_uname_output=Linux",
          "ac_cv_small_wchar_t=no",
        ]
      end

      ok = run_command("configure.log", configure_argv)
      return false if !ok

      ok = run_command("build.log", ["make", "-j#{BUILD_PAR}"])
      return false if !ok

      ok = run_command("install.log", ["make", "install"])
      return false if !ok
    end

    # Post-install: package runtime files and compress the binary
    chdir("install") do
      cp_r("../runtime", ".")

      chdir("runtime") do
        rm_rf(["doc", "lang", "tutor", "spell"])
      end

      ok = system("tar", "cfz", "vr.tgz", "runtime")
      return false if !ok
      rm_rf("runtime")

      cp("bin/vim", "vim")
      ok = system("gzip", "--best", "vim")
      return false if !ok
      File.chmod(0644, "vim.gz")
    end

    return true
  end
end

pkgmgr.register(VimPackage.new())
