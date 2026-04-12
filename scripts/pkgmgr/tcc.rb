# SPDX-License-Identifier: BSD-2-Clause

# TinyCC cross-compilation for Tilck.
#
# The fundamental challenge is a triple-cross scenario:
#
#   Build host:             x86_64-linux
#   TCC will run on:        i386-linux (Tilck)
#   TCC binaries target:    i386-linux (Tilck)
#
# TCC's build system conflates "host" and "target", making true
# cross-compilation difficult. Two problems must be solved:
#
#   1. c2str.exe: a host tool that TCC tries to build with $(CC), which
#      during cross builds is the cross-compiler. Fix: patch the Makefile
#      to use the system gcc for c2str.
#
#   2. libtcc1.a: the TCC runtime library, normally built by running the
#      just-built tcc binary. In a cross build, that binary is i386 and
#      cannot run on the x86_64 host. Fix: pass i386-libtcc1-usegcc=yes
#      to make, which uses the cross-GCC instead.
#
# The old bash script worked around problem #2 by installing 32-bit glibc
# on the host and running the i386 binary via the kernel's compat layer.
# This was fragile and broke with GCC >= 10.3. The usegcc approach avoids
# executing cross-compiled binaries entirely.

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class TccPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  TCC_URL = "git://repo.or.cz/tinycc.git"

  def initialize
    super(
      name: 'tcc',
      url: TCC_URL,
      on_host: false,
      is_compiler: false,
      arch_list: {
        "i386" => ALL_ARCHS["i386"],
        "riscv64" => ALL_ARCHS["riscv64"],
      },
      dep_list: []
    )
  end

  def expected_files = [
    ["tcc",               false],
    ["libtcc1.a",         false],
    ["include",           true],
    ["tcclib.h",          false],
    ["examples",          true],
  ]

  # Override install_impl because the default one uses download_file for
  # non-GitHub URLs, but TCC uses git:// protocol and needs git clone.
  def install_impl(ver)

    info "Install #{name} version: #{ver}"

    if installed? ver
      info "Package already installed, skip"
      return nil
    end

    ok = Cache::download_git_repo(url, tarname(ver), git_tag(ver))
    return false if !ok

    pkgmgr.with_cc() do |arch_dir|
      chdir_package_base_dir(arch_dir) do
        ok = Cache::extract_file(tarname(ver), ver_dirname(ver))
        return false if !ok
        ok = chdir_install_dir(arch_dir, ver) do
          d = mkpathname(getwd)
          ok = apply_patches(ver)
          return false if ok == false
          ok = install_impl_internal(d)
          ok = check_install_dir(d, true) if ok
        end
      end
    end
    return ok
  end

  def install_impl_internal(install_dir)

    arch = default_arch.gcc_tc    # "i686" or "riscv64"
    cpu = default_arch.name       # "i386" or "riscv64"

    # Tilck runtime paths: where TCC looks for crt*.o and libraries at runtime
    tilck_lib = "/lib/#{arch}-tilck-musl"

    # Compute DEF_GITHASH from saved git metadata (the .git dir was deleted
    # during cache packaging).
    make_vars = []
    if File.file?(".ref_short")
      githash = File.read(".ref_short").strip
    elsif File.file?(".our_tag")
      githash = File.read(".our_tag").strip
    end

    if githash
      make_vars << "DEF_GITHASH=-DTCC_GITHASH=\\\"#{githash}\\\""
    end

    # TCC's configure picks up CC from the environment (line 58) and then
    # --cross-prefix prepends to it again (line 295). Clear CC/AR so that
    # configure starts from "gcc"/"ar" and --cross-prefix works correctly.
    with_saved_env(%w[CC AR]) do

      ENV.delete("CC")
      ENV.delete("AR")

      configure_argv = [
        "./configure",
        "--cross-prefix=#{arch}-linux-",
        "--cpu=#{cpu}",
        "--enable-static",
        "--config-bcheck=no",
        "--config-backtrace=no",
        "--prefix=/",
        "--extra-ldflags=-static",
        "--crtprefix=#{tilck_lib}",
        "--libpaths=#{tilck_lib}",
      ]

      # macOS: TCC's configure auto-detects Darwin and sets CONFIG_OSX,
      # which adds macOS-only linker flags (-flat_namespace) that the
      # GNU cross-linker rejects. Force the target OS to Linux since
      # we're cross-compiling for Tilck.
      if OS == "Darwin"
        configure_argv << "--targetos=Linux"
      end

      ok = run_command("configure.log", configure_argv)
      return false if !ok
    end

    # Build TCC. The key flag is <cpu>-libtcc1-usegcc=yes which makes the
    # lib/Makefile use $(CC) (the cross-GCC) to compile libtcc1.a instead
    # of trying to run the just-built tcc binary on the host.
    ok = run_command("build.log", [
      "make",
      "#{cpu}-libtcc1-usegcc=yes",
      *make_vars,
    ])
    return false if !ok

    if !File.file?("tcc")
      error "Build succeeded but tcc binary not found"
      return false
    end

    # Strip the binary
    ok = system("#{arch}-linux-strip", "--strip-all", "tcc")
    if !ok
      error "Failed to strip tcc"
      return false
    end

    return true
  end
end

pkgmgr.register(TccPackage.new())
