# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class BusyBoxPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  CONFIG_FILE = MAIN_DIR / "other" / "busybox.config"

  def initialize
    super(
      name: 'busybox',
      url: "https://busybox.net/downloads",
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,
      dep_list: []
    )
  end

  def tarname(ver) = "#{name}-#{ver}.tar.bz2"

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

  private
  def fix_config_file
    data = File.read(".config")
    lines = data.lines()
    lines = lines[4...] # drop the first 4 lines
    lines.select! { |x| !x.strip().blank? } # drop the empty lines
    lines.select! { |x| !x.index("CONFIG_").nil? } # drop comment lines
    lines.map! { |x| x.rstrip() }

    # Do a reverse stable sort on the lines array, comparing the binary version
    # of the strings, which is what we did in the past with:
    #
    #     LC_ALL=C sort -sr .config > .config_sorted
    #
    lines = stable_sort(lines) { |x, y| -(x.b <=> y.b) }
    File.write(".config", lines.join("\n") + "\n")
  end

end

pkgmgr.register(BusyBoxPackage.new())
