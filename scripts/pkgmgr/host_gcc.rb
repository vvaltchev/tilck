# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'portability'
require 'tmpdir'

HOST_GCC_SOURCE = SourceRef.new(
  name: 'host_gcc',
  url:  'https://ftp.gnu.org/gnu/gcc',
  tarname: ->(ver) { "gcc-#{ver}.tar.xz" },

  # GCC publishes each release in its own directory, so the version
  # appears twice in the remote path but once in the cache filename.
  remote_tarname: ->(ver) { "gcc-#{ver}/gcc-#{ver}.tar.xz" },
)

#
# host_gcc: the compiler the host stack is built with.
#
# Built by the SYSTEM compiler, once. There is no bootstrap stage and
# no second pass, because we build for the host's own triple: the
# system compiler is already valid for the target, glibc was compiled
# by it directly, and this build simply links against that glibc via
# --with-sysroot. A cross toolchain needs five stages to break the
# gcc/glibc cycle; that cycle does not exist here.
#
# A :distro package, like binutils: these binaries are built by the
# system compiler and link the system libc, and the tier describes what
# a package's own binaries depend on. What matters for portability is
# the TARGET runtime this produces — libgcc, libstdc++ — which is
# compiled against our glibc and lands in the sysroot.
#
# Supported majors are 11 through 16 at the latest point release of
# each. Installing more than one is expected: each gets its own
# portable/gcc-<ver>/ stack, and HOST_VER_GCC selects which one the
# stack is built with.
#
# See docs/plans/portable-host-stack.md.
#
class HostGccPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  # Latest point release of each supported major, per ftp.gnu.org.
  #
  # All six have been built from source against glibc 2.41, each into
  # its own host stack, and each verified to report its own sysroot
  # and to produce binaries resolving nothing outside the toolchain:
  #
  #   11.5.0  12.5.0  13.4.0   --disable-libsanitizer (no crypt.h)
  #   14.4.0  15.3.0  16.2.0   full build
  #
  # This list used to claim six versions on the strength of having
  # built one, and three of them turned out to be broken. Adding a
  # version means building it.
  SUPPORTED = [
    Ver("11.5.0"), Ver("12.5.0"), Ver("13.4.0"),
    Ver("14.4.0"), Ver("15.3.0"), Ver("16.2.0"),
  ].freeze

  #
  # The maths libraries each GCC wants, by major.
  #
  # They genuinely differ -- 11 wants gmp 6.1.0 where 16 wants 6.3.0 --
  # and getting one wrong is not a build failure but a compiler built
  # against a library its own sources were never tested with.
  #
  # This is a declared table and has to be: dependency resolution
  # happens before any source is fetched, so it cannot be read out of
  # the tarball at the time it is needed. It is not left to rot,
  # though -- test_gcc_prereqs.rb reads contrib/download_prerequisites
  # out of every cached GCC tarball and fails if this table disagrees
  # with what upstream says.
  #
  PREREQ_NAMES = %w[host_gmp host_mpfr host_mpc host_isl].freeze

  PREREQS = {
    Ver("11.5.0") => { gmp: "6.1.0", mpfr: "3.1.6", mpc: "1.0.3",
                       isl: "0.18" },
    Ver("12.5.0") => { gmp: "6.2.1", mpfr: "4.1.0", mpc: "1.2.1",
                       isl: "0.24" },
    Ver("13.4.0") => { gmp: "6.2.1", mpfr: "4.1.0", mpc: "1.2.1",
                       isl: "0.24" },
    Ver("14.4.0") => { gmp: "6.2.1", mpfr: "4.1.0", mpc: "1.2.1",
                       isl: "0.24" },
    Ver("15.3.0") => { gmp: "6.2.1", mpfr: "4.1.0", mpc: "1.2.1",
                       isl: "0.24" },
    Ver("16.2.0") => { gmp: "6.3.0", mpfr: "4.2.2", mpc: "1.3.1",
                       isl: "0.24" },
  }.freeze

  def initialize
    super(
      name: 'host_gcc',
      source: HOST_GCC_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_binutils', true),
        Dep('host_glibc', true),

        # Named here without a version, because this list is the
        # STRUCTURE the dependency graph is built from and the graph
        # has no version in hand. Which version belongs to which GCC
        # is dep_list_for's job.
        *PREREQ_NAMES.map { |n| Dep(n, true) },
      ],
      default: false,
    )
  end

  #
  # The four maths libraries are pinned per GCC version, which is why
  # this varies by version at all: one resolution binds one version of
  # each package, so `-s host_gcc:11.5.0` gets gmp 6.1.0 and
  # `-s host_gcc:16.2.0` gets 6.3.0, each in its own directory beside
  # the other.
  #
  def dep_list_for(ver = nil)

    p = PREREQS[ver || default_ver]
    return dep_list if p.nil?

    # Replace rather than append: the same package named twice, once
    # bare and once pinned, would leave which one wins to the order
    # the solver happens to walk them in.
    base = dep_list.reject { |d| PREREQ_NAMES.include?(d.name) }

    return base + PREREQ_NAMES.map { |n|
      Dep(n, true, ver: Ver(p[n.delete_prefix("host_").to_sym]))
    }
  end

  def host_world_root? = true

  # Where the host world runs: x86_64 Linux, for now. Everything only
  # this world needs follows (Package#host_supported?). Other host
  # arches join once every package of the world has been built and
  # exercised there. Overrides, not constructor arguments: a statement
  # about where the world runs is not part of any recipe.
  def host_os_list = ["linux"]
  def host_arch_list = ["x86_64"]
  def pkg_dirname = "gcc"

  # A compiler belongs to ITS OWN stack, not to whichever one
  # HOST_VER_GCC currently names.
  #
  # This is the whole point of keying stacks by compiler version. With
  # the base class's answer, `-s host_gcc:11.5.0` produced a compiler
  # installed as 11.5.0 but configured --with-sysroot=.../gcc-14.4.0/
  # and with 14.4.0's loader baked into its specs: a compiler bound to
  # another compiler's stack, using its libstdc++. Worse, the binding
  # is fixed at build time while the default it was taken from is
  # mutable, so switching HOST_VER_GCC afterwards left the compiler
  # pointing at a stack it no longer belonged to, with nothing
  # detecting the disagreement.
  def stack_gcc_ver(ver = nil) = ver || scope.stack

  # An install of the stack compiler belongs to the stack it defines,
  # although it lives in the distro's env: what it needs -- its glibc,
  # its binutils -- is in that stack, and asked about at the current
  # one, host_gcc 11.5.0 needed the glibc of 14.4.0 and --autoremove
  # offered to take the glibc of every other stack.
  def stack_of_install(inst) = stack_gcc_ver(inst.ver)

  # The host stack this version defines: its manifest names this
  # compiler, where this host keeps it, the glibc it was built with,
  # and the host that built it.
  def stacks_defined(ver, against, compiler_at: nil)
    stack = pkgmgr.stack_coords(ver, host: scope.host)
    at = compiler_at || coords(ver)
    return [[stack, StackManifest.new(kind: :host, compiler_name: name,
                                      compiler_ver: ver,
                                      compiler_at: at.to_s,
                                      libc: "host_glibc",
                                      libc_ver: against["host_glibc"],
                                      host: scope.host.to_s)]]
  end

  # The stack's compiler runtime comes from the gcc that NAMES the
  # stack, so composing gcc-11.5.0 grafts 11.5.0's libstdc++ even when
  # another gcc is the default. Its binaries go in beside, at usr/bin:
  # gcc is a :distro package and lives outside the stack, but it is
  # the stack's compiler, and a sysroot is the stack's merged prefix.
  # A link is enough -- gcc finds its own pieces through the binary's
  # real location, not the name it was invoked by.
  def sysroot_fragments(gcc_ver = nil)

    gcc_ver ||= default_ver
    dir = stack_compiler_dir(gcc_ver)
    return [] if dir.nil?

    lib64 = dir / "install" / "lib64"
    frags = [[dir / "install" / "bin", "usr/bin"]]
    frags << [lib64, "usr/lib"] if lib64.directory?
    return frags
  end

  def expected_files(ver = nil) = [
    ["install/bin/gcc", false],
    ["install/bin/g++", false],
    ["install/lib/gcc", true],
  ]

  # The C++ half of the proof, which can only run once the sysroot has
  # been composed: libstdc++ and libgcc_s reach it through this
  # package's own graft, so at install time they are not there yet.
  # Without this a broken graft passes the install and fails later, at
  # runtime, in whatever package first links C++.
  def post_sysroot_check(gcc_ver = nil)

    gcc_ver ||= default_ver
    inst = find_install(gcc_ver)
    return true if inst.nil?

    ok = false

    Dir.mktmpdir("gcc-cxx-check-") do |d|
      src = File.join(d, "t.cpp")
      bin = File.join(d, "t")
      File.write(src, "#include <string>\nint main(){std::string s;return s.size();}\n")

      if !system("#{inst.path}/install/bin/g++", "-O0", "-o", bin, src,
                 out: File::NULL, err: File::NULL)
        error "the installed g++ cannot compile a trivial C++ program"
        next
      end

      loader = stack_loader(gcc_ver)
      readelf = dep_install_dir("host_binutils") / "install/bin/readelf"
      refs = Portability.read_refs(bin, readelf: readelf.to_s)
      resolved = Portability.resolve_libs(bin, loader: loader)

      violations = Portability.check_refs(
        bin, interp: refs[:interp], rpaths: refs[:rpaths],
        resolved: resolved || {}, allowed: [TC]
      )

      if !violations.empty?
        error "g++ produces non-portable binaries:"
        violations.each { |v| error "  #{v.kind}: #{v.detail}" }
        next
      end

      info "Verified: g++ produces portable binaries (libstdc++ included)"
      ok = true
    end

    return ok
  end

  # The three decisions below were each wrong once, in the same way:
  # they asked about default_ver instead of the version being
  # installed. They live here, as functions of an explicit version,
  # so a unit test can ask them directly — buried inside a sixty-line
  # install method they were only reachable by building a compiler.

  # Is this a version we know how to build?
  def supported_version?(ver) = SUPPORTED.include?(ver)
  def installable_versions = SUPPORTED

  # The default version IS the stack in effect. HOST_VER_GCC supplies
  # it when nothing else does, but -H names another stack, and then
  # the compiler the stack's packages are built against has to be that
  # one -- otherwise `-H 16.2.0 -s host_qemu` would place QEMU in
  # gcc-16.2.0 while building it against the 14.4.0 the file names.
  def default_ver = scope.stack

  # The dynamic loader belonging to a given stack.
  #
  # A method rather than an expression repeated at each site: the copy
  # in post_sysroot_check referred to `sysroot`, a local of
  # install_impl_internal, and raised NameError instead of returning
  # false — aborting five unrelated builds.
  # Where a stack's glibc puts it, relative to the sysroot. The recipe
  # names it as $STACK_SYSROOT/LOADER; the audit asks stack_loader.
  LOADER = "usr/lib/ld-linux-x86-64.so.2"

  def stack_loader(gcc_ver)
    return "#{pkgmgr.stack_sysroot(gcc_ver)}/#{LOADER}"
  end

  # Configure flags that depend on which version is being built.
  #
  # glibc removed libcrypt and crypt.h in 2.39; it lives in the
  # separate libxcrypt project now. GCC below 14 includes <crypt.h>
  # unconditionally from libsanitizer and cannot be built against a
  # glibc that new. The boundary is measured, not assumed: against
  # glibc 2.41, 11.5.0/12.5.0/13.4.0 fail and 14.4.0 builds.
  def version_conf_args(ver)

    args = []
    args << "--disable-libsanitizer" if ver < Ver("14.0.0")
    return args
  end

  # A version nobody supports is refused before this is asked:
  # installable_versions names SUPPORTED, and a request has to name
  # one of them.
  def build_steps(ver = default_ver) = [

    # GCC refuses to be configured in its own source tree.
    Mkdir(path: "build"),

    Within(dir: "build", steps: [
      Run(log: "configure.log", argv: [
        "../configure",
        "--prefix=$PREFIX",

        # The whole point: headers and libraries resolve inside our
        # sysroot, so libgcc and libstdc++ are built against OUR glibc
        # and anything this compiler builds looks there and nowhere
        # else. $STACK_SYSROOT is the stack this compiler DEFINES, not the
        # sysroot at its own :distro coordinates.
        "--with-sysroot=$STACK_SYSROOT",

        # Use the binutils we built, not whatever the host happens to
        # have. --with-build-time-tools covers the build itself; -as
        # and -ld cover what the finished compiler invokes. Through
        # the package, not the sysroot: binutils is a :distro package
        # and deliberately not part of the sysroot at all.
        "--with-build-time-tools=$host_binutils/install/bin",
        "--with-as=$host_binutils/install/bin/as",
        "--with-ld=$host_binutils/install/bin/ld",

        # The maths libraries, as packages we built at the versions
        # THIS GCC asks for, rather than tarballs it downloads for
        # itself mid-build. See PREREQS.
        *prereq_flags,

        "--enable-languages=c,c++",

        # Nothing here wants translations, and they would pull in the
        # host's gettext.
        "--disable-nls",

        # Multilib would need a 32-bit glibc in the sysroot as well,
        # and nothing in the QEMU stack is 32-bit.
        "--disable-multilib",

        # Bootstrapping rebuilds GCC three times with itself, which is
        # how GCC validates a compiler change. We are not changing
        # GCC, and it triples an already long build.
        "--disable-bootstrap",

        # With no bootstrap, the system compiler builds all of it, and
        # it has to be told which C++ that is: under GCC 16's default
        # C++20, GCC 11's libcody stops at `S2C(u8" ")` -- a char8_t
        # array where the code expects char. GCC's own gcc/ directory
        # picks no dialect of its own either. See
        # Package#host_compiler_gnu17.
        *host_compiler_gnu17,

        # --disable-libsanitizer below 14: see version_conf_args.
        *version_conf_args(ver),
      ]),
      Run(log: "build.log", argv: ["make", "-j$PAR"]),
      Run(log: "install.log",
          argv: ["make", "install", "DESTDIR=$DESTDIR"]),
    ]),

    Move(from: "$DESTDIR$PREFIX", to: "$INSTALL/install"),

    # POINT THE FINISHED COMPILER AT OUR LOADER BY DEFAULT.
    #
    # --with-sysroot gets link time right on its own: the search
    # paths, -print-file-name=libc.so and the -L flags all resolve
    # inside the sysroot. What it does NOT change is the ELF
    # interpreter, which GCC bakes from a hardcoded path in its link
    # spec. A binary built without this links against our glibc and
    # is then loaded by the system one -- the worst of both, and
    # invisible unless you ask the right loader about it: the system
    # ldd reports the system libc for such a binary regardless of what
    # it would really load. Rewriting the driver's specs makes the
    # default correct rather than leaving every consumer to remember
    # -Wl,--dynamic-linker=.
    #
    # -print-file-name=specs is no good for finding where: for a file
    # that does not exist yet -- which is always, since we are about
    # to create it -- gcc echoes the bare name back. The "install:"
    # line of -print-search-dirs is the directory it actually looks
    # in.
    Capture(bind: "search_dirs",
            argv: ["$INSTALL/install/bin/gcc", "-print-search-dirs"]),
    Extract(bind: "specs_dir", from: "$search_dirs",
            pattern: /^install:\s*(.+)$/),
    Capture(bind: "specs", argv: ["$INSTALL/install/bin/gcc", "-dumpspecs"]),

    # Two rewrites, and each MUST match: a spec that no longer
    # mentions the system loader means the hardcoded interpreter
    # moved, and a spec with no *link: section has nowhere to put an
    # rpath. Either would once have silently done nothing.
    #
    # The rpath records the library search path in the binaries
    # themselves. Without it, portability is an accident of which
    # loader happens to run: our ld.so has the sysroot compiled in as
    # its default search path and finds our libraries, the SYSTEM
    # ld.so finds the system's, and the binary says nothing either
    # way -- one LD_LIBRARY_PATH and our own loader loads the system's
    # libstdc++. DT_RPATH rather than DT_RUNPATH, because RPATH is
    # searched BEFORE LD_LIBRARY_PATH and RUNPATH after it; only the
    # former is immune. --disable-new-dtags is our binutils' default
    # today, but that is a build-time default rather than a promise,
    # and silently getting RUNPATH would silently restore the hole.
    Extract(bind: "link_line", from: "$specs",
            pattern: /^\*link:\n([^\n]*)$/),
    #
    # The rpath first, matched against the *link: line as captured;
    # the loader second, since it appears inside that same line and
    # rewriting it first would leave the capture matching nothing.
    Transform(bind: "specs", from: "$specs", subs: [
      ["*link:\n$link_line",
       "*link:\n$link_line %{!static:-rpath $STACK_SYSROOT/usr/lib " \
       "--disable-new-dtags}"],
      [SYSTEM_LOADER, "$STACK_SYSROOT/#{LOADER}"],
    ]),
    Mkdir(path: "$specs_dir"),
    Write(path: "$specs_dir/specs", text: "$specs"),

    # A GCC build tree is several GB; the compiler is a few hundred
    # MB.
    Prune(),
  ]

  # Make the compiler prove itself once the install is in place.
  # Everything above is a claim about how GCC was configured; this is
  # the only check that observes what it actually produces -- and a
  # toolchain quietly emitting system-linked binaries would poison
  # every package built after it.
  def postconditions(ver = default_ver) = [PortableBinaries.new]

  private

  # The system loader GCC hardcodes into its link spec on this host.
  SYSTEM_LOADER = "/lib64/ld-linux-x86-64.so.2"

  #
  # The installed gcc produces portable binaries: compile a trivial
  # program with it and check its interpreter, its rpath and every
  # library the stack's loader resolves for it live under the
  # toolchain. Cheap, and run once the install is where it lives.
  #
  class PortableBinaries < Postcondition::Base

    def check(pkg, dir)

      install = dir / "install"
      gcc_ver = pkg.installing_ver(dir)
      ok = false

      Dir.mktmpdir("gcc-portable-check-") do |d|
        src = File.join(d, "t.c")
        bin = File.join(d, "t")
        File.write(src, "int main(void){return 0;}\n")

        if !system("#{install}/bin/gcc", "-O0", "-o", bin, src,
                   out: File::NULL, err: File::NULL)
          error "the installed gcc cannot compile a trivial program"
          next
        end

        loader = pkg.stack_loader(gcc_ver)
        readelf = pkg.dep_install_dir("host_binutils") /
                  "install" / "bin" / "readelf"
        refs = Portability.read_refs(bin, readelf: readelf.to_s)
        resolved = Portability.resolve_libs(bin, loader: loader)

        violations = Portability.check_refs(
          bin, interp: refs[:interp], rpaths: refs[:rpaths],
          resolved: resolved || {}, allowed: [TC]
        )

        if !violations.empty?
          error "gcc #{gcc_ver} produces non-portable binaries:"
          violations.each { |v| error "  #{v.kind}: #{v.detail}" }
          next
        end

        info "Verified: gcc produces portable binaries by default"
        ok = true
      end

      return ok
    end

    def describe = "the installed gcc produces portable binaries"
  end

  # --with-gmp and friends. Each is a token: what the resolver bound
  # for this request is what the token resolves to, and the resolver
  # bound what dep_list_for pinned from PREREQS -- so the configure
  # line follows the tree, and the table is only our statement of
  # intent about it.
  def prereq_flags
    return PREREQ_NAMES.map { |n|
      "--with-#{n.delete_prefix("host_")}=$#{n}/install"
    }
  end
end

pkgmgr.register(HostGccPackage.new())
