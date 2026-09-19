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

  def expected_files(ver = nil) = [
    ["install/vim.gz", false],
    ["install/vr.tgz", false],
  ]

  def build_steps(ver = default_ver)

    arch = default_arch.gcc_tc

    configure_argv = [
      "./configure",
      "--prefix=$INSTALL/install",
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

    return [

      # ncurses through the dependency token: it resolves in the tree
      # of the arch being built for, so `-s vim -a <arch>` reads the
      # matching per-arch install.
      Within(env: {
        "CFLAGS"   => "-ggdb -Os",
        "LDFLAGS"  => "-static -L$ncurses/install/lib -lncurses",
        "CPPFLAGS" => "-I$ncurses/install/include " \
                      "-I$ncurses/install/include/ncurses",
      }, steps: [
        Run(log: "configure.log", argv: configure_argv),
        Run(log: "build.log", argv: ["make", "-j$PAR"]),
        Run(log: "install.log", argv: ["make", "install"]),
      ]),

      # Post-install: package runtime files and compress the binary
      Within(dir: "install", steps: [
        Copy(from: "../runtime", to: "."),
        Remove(paths: ["runtime/doc", "runtime/lang",
                       "runtime/tutor", "runtime/spell"]),
        Run(argv: ["tar", "cfz", "vr.tgz", "runtime"]),
        Remove(paths: ["runtime"]),
        Copy(from: "bin/vim", to: "vim"),
        Run(argv: ["gzip", "--best", "vim"]),
        Chmod(path: "vim.gz", mode: 0644),
      ]),
    ]
  end
end

pkgmgr.register(VimPackage.new())
