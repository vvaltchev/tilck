# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT A HOST'S BINARIES LOOK LIKE: the facts of one ABI, by machine.
#
# The host stack -- our glibc, our GCC, everything built into a
# stack -- and the portability audit that judges it need a handful of
# facts about the machine the stack runs on: which ELF machine its
# binaries carry, where its glibc puts the dynamic loader, where the
# distro keeps the loader GCC hardcodes into its link spec, which
# library directories a hostile environment would point at, and
# where GCC installs its own runtime. They were constants in four
# files, each x86_64-glibc by spelling, so bringing the stack up on
# another host meant finding them all. This is the one table, keyed
# by Host#machine (host.rb) and read through scope.host.abi.
#
# Nothing here changes what is on disk: the x86_64 row is what the
# constants said. A row is a new host; a host with no row has no
# stack, which host_gcc's own host_arch_list says already.
#
HostABI = Data.define(:machine, :elf_machine, :loader, :system_loader,
                      :libdirs, :gcc_libdir) do

  # e_machine of an ELF this host executes (ELF spec, EM_*).
  #
  # loader          where our glibc installs the dynamic loader,
  #                 relative to the stack's sysroot; GCC's specs and
  #                 the audit name it
  # system_loader   the distro's, which GCC hardcodes into its link
  #                 spec on this host and the specs rewrite
  # libdirs         the distro's library directories: what a hostile
  #                 LD_LIBRARY_PATH points at, so that a binary has to
  #                 say for itself where its libraries live
  # gcc_libdir      the directory under a GCC install holding the
  #                 target runtime (libgcc_s, libstdc++)
  TABLE = {
    "linux-x86_64" => new(
      machine: "linux-x86_64",
      elf_machine: 62,
      loader: "usr/lib/ld-linux-x86-64.so.2",
      system_loader: "/lib64/ld-linux-x86-64.so.2",
      libdirs: ["/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu",
                "/usr/lib64", "/lib64", "/usr/lib", "/lib"],
      gcc_libdir: "lib64",
    ),
  }.freeze

  # The ABI of `machine`, or nil for a host this table does not know
  # -- one the stack cannot be built on yet.
  def self.for(machine) = TABLE[machine]

  def to_s = machine
end
