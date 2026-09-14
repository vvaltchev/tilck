# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

BUSYBOX_SOURCE = SourceRef.new(
  name: 'busybox',
  url:  "https://busybox.net/downloads",
  tarname: ->(ver) { "busybox-#{ver}.tar.bz2" },
)

class BusyBoxPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  CONFIG_FILE = MAIN_DIR / "other" / "busybox.config"

  def initialize
    super(
      name: 'busybox',
      source: BUSYBOX_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS.values,
      dep_list: [Dep('host_ncurses', true)],
      default: true,
    )
  end

  def expected_files(ver = nil) = [
    ["busybox", false],
  ]

  # The build ends by putting .config back in the normal form the
  # source's copy is kept in: make rewrites it with a dated header
  # and every symbol in Kconfig order, and userapps/CMakeLists.txt
  # compares the two files byte for byte to know the build is the
  # source's. (.last_build_config, which the build also used to
  # write, is read by nothing and is not written.)
  def build_steps(ver = default_ver) = [
    Copy(from: src_path(CONFIG_FILE), to: ".config"),
    Run(log: "build.log", argv: ["make", "V=1", "-j$PAR"]),
    Normalize(path: ".config", form: "kconfig"),
  ]

  def configurable? = true

  def config_impl
    # configure runs this in the installed version's directory.
    be = deps_build_env.expand(BuildCtx.new(self, Pathname.pwd))

    ok = system(be.env, "make", *be.kconfig_make_vars, "menuconfig")
    return false if !ok

    fix_config_file

    print "Update #{CONFIG_FILE.basename} with the new config? [Y/n]: "
    answer = STDIN.gets&.strip&.downcase

    if answer.nil? || answer.empty? || answer == "y"
      cp ".config", CONFIG_FILE.to_s
      info "Source file #{CONFIG_FILE} UPDATED"
    end

    # Rebuild with the new configuration
    info "Rebuilding #{name}..."
    ok = run_command("build.log", [ "make", "V=1", "-j#{BUILD_PAR}" ])
    return false if !ok

    fix_config_file
    cp ".config", ".last_build_config"
    return true
  end

end

pkgmgr.register(BusyBoxPackage.new())
