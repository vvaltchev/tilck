# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class MicropythonPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'micropython',
      url: GITHUB + '/micropython/micropython',
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def expected_files = [
    ["ports/unix/build-standard/micropython", false],
  ]

  def install_impl_internal(install_dir)
    ok = build_mpy_cross
    return false if !ok
    return build_unix_port
  end

  private

  # mpy-cross must be compiled for the host, not the target.
  def build_mpy_cross
    cc_vars = %w[CC CXX AR NM RANLIB CROSS_PREFIX CROSS_COMPILE]
    with_saved_env(cc_vars) do
      cc_vars.each { |v| ENV.delete(v) }
      chdir("mpy-cross") do
        return run_command("build.log", [
          "make", "V=1", "-j#{BUILD_PAR}",
        ])
      end
    end
  end

  def build_unix_port
    chdir("ports/unix") do
      ok = run_command("make_submodules.log", ["make", "submodules"])
      return false if !ok

      with_saved_env(["LDFLAGS_EXTRA"]) do
        ENV["LDFLAGS_EXTRA"] = "-static"
        return run_command("build.log", [
          "make", "V=1",
          "MICROPY_PY_FFI=0",
          "MICROPY_PY_THREAD=0",
          "MICROPY_PY_BTREE=0",
          "-j#{BUILD_PAR}",
        ])
      end
    end
  end
end

pkgmgr.register(MicropythonPackage.new())
