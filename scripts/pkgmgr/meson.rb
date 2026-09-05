# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

MESON_SOURCE = SourceRef.new(
  name: 'meson',
  url:  GITHUB + '/mesonbuild/meson',
)

#
# host_meson: the build system glib2, pixman, pango, cairo and most of
# the rest of the GTK stack are configured with.
#
# A :distro package for the same reason as ninja. Ubuntu 22.04 — the
# oldest build host we support — ships meson 0.61, too old for a
# current glib, so taking it from the system would put a floor under
# the stack that varies by machine.
#
# Meson is pure Python and is not "built": the release tree runs as-is
# from meson.py. It is installed as that tree plus a wrapper on PATH,
# rather than through pip, so nothing depends on the host's package
# tooling or writes outside the toolchain.
#
# It does depend on the host's python3, which is the same compromise
# the system compiler is: a build-time input that leaves no trace in
# what is produced.
#
class HostMesonPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_meson',
      source: MESON_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_ninja', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  def expected_files(ver = nil) = [
    ["install/bin/meson", false],
    ["install/lib/meson/meson.py", false],
  ]

  def build_env(ver)

    bin = install_prefix(ver) / "install" / "bin"

    return BuildEnv.new(
      bin_dirs: [bin],
      extra_env: { "MESON" => "#{bin}/meson" },
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  def install_impl_internal(install_dir)

    # The wrapper has to name the path meson will live at, not the
    # staging one we are standing in: the staging directory stops
    # existing the moment the atomic move completes. expected_files
    # cannot catch this — the file is present either way, and only its
    # contents are wrong — so the check at the end runs it.
    final = final_install_prefix(install_dir)

    libdir = "#{install_dir}/install/lib/meson"
    bindir = "#{install_dir}/install/bin"

    FileUtils.mkdir_p(libdir)
    FileUtils.mkdir_p(bindir)

    # Everything except the install prefix we are building into.
    Dir.children(".").each { |e|
      next if e == "install"
      FileUtils.cp_r(e, libdir)
    }

    # A wrapper rather than a symlink: meson locates its own modules
    # relative to meson.py, and running it through a symlink from
    # bin/ would put that resolution one directory away from the tree.
    wrapper = "#{bindir}/meson"
    File.write(wrapper, <<~SH)
      #!/bin/sh
      exec python3 "#{final}/lib/meson/meson.py" "$@"
    SH
    FileUtils.chmod(0755, wrapper)

    prune_build_tree

    # Run it, with the staging tree standing in for the final path, so
    # a wrapper that cannot start is caught here rather than by the
    # first package that tries to configure with it.
    check = File.read(wrapper).sub(final.to_s, "#{install_dir}/install")
    File.write("#{install_dir}/check-meson", check)
    FileUtils.chmod(0755, "#{install_dir}/check-meson")

    out = `#{install_dir}/check-meson --version 2>&1`.strip
    FileUtils.rm_f("#{install_dir}/check-meson")

    if !$?.success?
      error "the installed meson does not run: #{out}"
      return false
    end

    info "meson #{out} runs"
    return true
  end
end

pkgmgr.register(HostMesonPackage.new())
