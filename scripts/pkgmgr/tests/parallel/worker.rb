# SPDX-License-Identifier: BSD-2-Clause
#
# ONE PACKAGE MANAGER, IN ITS OWN PROCESS, installing one package into
# a throwaway tree: what test_parallel.rb runs several of at once.
#
# The tree is the one TCROOT_PARENT names (early_logic.rb), so that
# every path the package manager derives points into it; the source
# is a real tarball the test serves over HTTP, so that the fetch, the
# partial file, the pin check, the cache's record, the temporary
# directory and the staging directory are all the real thing, and
# contended for. The build itself is a fake: it sleeps, to hold the
# staging directory long enough for the others to arrive, and writes
# one file.
#
#   ruby worker.rb <name> <url> <file> <pin> <delay>
#
# Exit status is the install's: 0 when the package is installed at
# the end, whichever process did it.
#
require_relative '../../main'

# Never a real tree: the wrapper exports TCROOT, and a worker that
# inherited it would build into the developer's toolchain. The tree
# must be under the parent the test made.
if !TC.to_s.start_with?(ENV.fetch("TCROOT_PARENT") + "/")
  abort "worker: refusing to run against #{TC}"
end

name, url, file, pin, delay = ARGV

class ParallelFakePackage < Package
  def initialize(name, url, file, delay)
    @delay = delay.to_f
    super(name: name, on_host: true, is_compiler: false,
          host_tier: :portable, arch_list: ALL_HOST_ARCHS.values,
          dep_list: [],
          source: SourceRef.new(name: name, url: url,
                                tarname: ->(_v) { file },
                                fetch_via_git: false))
  end

  def default_ver = Ver("1.0")
  def expected_files(ver = nil) = [["built-by", false]]
  def build_steps(ver = default_ver) = [Run(log: "b.log", argv: ["true"])]

  def install_impl_internal(install_dir)
    sleep(@delay)
    File.write(install_dir / "built-by", "#{Process.pid}\n")
    return true
  end
end

pkgmgr.pins = { file => SourcePins.parse_pin(pin) }
pkgmgr.register(ParallelFakePackage.new(name, url, file, delay))
ok = pkgmgr.install(name)
exit(ok ? 0 : 1)
