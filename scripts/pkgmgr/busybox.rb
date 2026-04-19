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

  def expected_files = [
    ["busybox", false],
  ]

  def install_impl_internal(install_dir)

    cp CONFIG_FILE, ".config"
    ok = run_command("build.log", [ "make", "V=1", "-j#{BUILD_PAR}" ])
    return false if !ok

    fix_config_file
    cp ".config", ".last_build_config"
    return ok
  end

  def configurable? = true

  def config_impl
    make_vars, env = host_ncurses_build_flags

    ok = system(env, "make", *make_vars, "menuconfig")
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
