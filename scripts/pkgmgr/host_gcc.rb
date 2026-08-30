# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'hermeticity'
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
# host_gcc: the compiler the hermetic stack is built with.
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
# a package's own binaries depend on. What matters for hermeticity is
# the TARGET runtime this produces — libgcc, libstdc++ — which is
# compiled against our glibc and lands in the sysroot.
#
# Supported majors are 11 through 16 at the latest point release of
# each. Installing more than one is expected: each gets its own
# hermetic/gcc-<ver>/ stack, and HOST_VER_GCC selects which one the
# stack is built with.
#
# See docs/plans/hermetic-host-toolchain.md.
#
class HostGccPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  # Latest point release of each supported major, per ftp.gnu.org.
  #
  # All six have been built from source against glibc 2.41, each into
  # its own hermetic stack, and each verified to report its own sysroot
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
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED
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
  def stack_gcc_ver(ver = nil) = ver || pkgmgr.current_hermetic_stack

  # The stack's compiler runtime comes from the gcc that NAMES the
  # stack, so composing gcc-11.5.0 grafts 11.5.0's libstdc++ even when
  # another gcc is the default.
  def sysroot_fragments(gcc_ver = nil)

    gcc_ver ||= default_ver
    inst = find_install(gcc_ver)
    return [] if inst.nil?

    lib64 = inst.path / "install" / "lib64"
    return lib64.directory? ? [[lib64, "usr/lib"]] : []
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
      refs = Hermeticity.read_refs(bin, readelf: "#{binutils_bin_dir}/readelf")
      resolved = Hermeticity.resolve_libs(bin, loader: loader)

      violations = Hermeticity.check_refs(
        bin, interp: refs[:interp], rpaths: refs[:rpaths],
        resolved: resolved || {}, allowed: [TC]
      )

      if !violations.empty?
        error "g++ produces non-hermetic binaries:"
        violations.each { |v| error "  #{v.kind}: #{v.detail}" }
        next
      end

      info "Verified: g++ produces hermetic binaries (libstdc++ included)"
      ok = true
    end

    return ok
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  # The three decisions below were each wrong once, in the same way:
  # they asked about default_ver instead of the version being
  # installed. They live here, as functions of an explicit version,
  # so a unit test can ask them directly — buried inside a sixty-line
  # install method they were only reachable by building a compiler.

  # Is this a version we know how to build?
  def supported_version?(ver) = SUPPORTED.include?(ver)

  # The dynamic loader belonging to a given stack.
  #
  # A method rather than an expression repeated at each site: the copy
  # in post_sysroot_check referred to `sysroot`, a local of
  # install_impl_internal, and raised NameError instead of returning
  # false — aborting five unrelated builds.
  def stack_loader(gcc_ver)
    return "#{pkgmgr.hermetic_sysroot(gcc_ver)}/usr/lib/ld-linux-x86-64.so.2"
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

  def install_impl_internal(install_dir)

    # The version being INSTALLED, which is not default_ver when the
    # user named another one. Getting this wrong is silent: the checks
    # below would answer about a compiler nobody asked for.
    ver = installing_ver(install_dir)
    sysroot = pkgmgr.hermetic_sysroot(ver)

    if !supported_version?(ver)
      error "gcc #{ver} is not one of the supported versions: " +
            SUPPORTED.map(&:to_s).join(", ")
      return false
    end

    prefix = final_install_prefix(install_dir)
    destdir = "#{install_dir}/destdir"
    binutils = binutils_bin_dir

    # GCC downloads gmp, mpfr, mpc and isl for itself rather than
    # requiring them from the host. That is one fewer host dependency
    # and exactly the behaviour we want.
    ok = run_command("prereq.log", ["./contrib/download_prerequisites"])
    return false if !ok

    # GCC refuses to be configured in its own source tree.
    FileUtils.mkdir_p("build")

    conf = [
      "../configure",
      "--prefix=#{prefix}",

      # The whole point: headers and libraries resolve inside our
      # sysroot, so libgcc and libstdc++ are built against OUR glibc
      # and anything this compiler builds looks there and nowhere else.
      "--with-sysroot=#{sysroot}",

      # Use the binutils we built, not whatever the host happens to
      # have. --with-build-time-tools covers the build itself; the
      # -B flags below cover what the finished compiler invokes.
      "--with-build-time-tools=#{binutils}",
      "--with-as=#{binutils}/as",
      "--with-ld=#{binutils}/ld",

      "--enable-languages=c,c++",

      # Nothing here wants translations, and they would pull in the
      # host's gettext.
      "--disable-nls",

      # Multilib would need a 32-bit glibc in the sysroot as well, and
      # nothing in the QEMU stack is 32-bit.
      "--disable-multilib",

      # Bootstrapping rebuilds GCC three times with itself, which is
      # how GCC validates a compiler change. We are not changing GCC,
      # and it triples an already long build.
      "--disable-bootstrap",
    ]

    # glibc removed libcrypt and crypt.h in 2.39; it lives in the
    # separate libxcrypt project now. Older GCC includes <crypt.h>
    # unconditionally from libsanitizer and cannot be built against a
    # glibc that new:
    #
    #   libsanitizer/sanitizer_common/sanitizer_platform_limits_posix.cpp:
    #     fatal error: crypt.h: No such file or directory
    #
    # The boundary here is MEASURED, not assumed. An earlier version of
    # this comment claimed GCC 13 had fixed it upstream; 13.4.0 then
    # failed exactly like 11.5.0 and 12.5.0 did. Observed against glibc
    # 2.41: 11.5.0 fails, 12.5.0 fails, 13.4.0 fails, 14.4.0 builds.
    # 15.3.0 and 16.2.0 are untested, and being newer than the last
    # known-good they get no flag until something says otherwise.
    #
    # Supplying crypt.h instead would mean adding libxcrypt, which is an
    # ordinary library that has to be built AGAINST our glibc and
    # therefore needs our gcc — a real cycle, and not one the
    # same-triple trick breaks, because libxcrypt cannot be built by the
    # system compiler without linking the system libc.
    #
    # So the sanitizer runtimes are dropped for those versions only.
    # Nothing in the QEMU stack uses them, and the alternative is not
    # supporting those compilers at all.
    conf.concat(version_conf_args(ver))

    ok = false
    chdir("build") do
      ok = run_command("configure.log", conf)
      next if !ok

      ok = run_command("build.log", ["make", "-j#{BUILD_PAR}"])
      next if !ok

      ok = run_command("install.log",
                       ["make", "install", "DESTDIR=#{destdir}"])
    end

    return false if !ok

    FileUtils.mv("#{destdir}#{prefix}", "#{install_dir}/install")

    return false if !install_hermetic_specs("#{install_dir}/install", ver)
    return false if !verify_produces_hermetic_binaries(
                       "#{install_dir}/install", ver)

    # A GCC build tree is several GB; the compiler is a few hundred MB.
    prune_build_tree
    return true
  end

  private

  # The system loader GCC hardcodes into its link spec on this host.
  SYSTEM_LOADER = "/lib64/ld-linux-x86-64.so.2"

  # Point the finished compiler at OUR loader by default.
  #
  # --with-sysroot gets link time right on its own: the search paths,
  # -print-file-name=libc.so and the -L flags all resolve inside the
  # sysroot. What it does NOT change is the ELF interpreter, which GCC
  # bakes from a hardcoded path in its link spec. A binary built
  # without this links against our glibc and is then loaded by the
  # system one — the worst of both, and invisible unless you ask the
  # right loader about it: the system ldd reports the system libc for
  # such a binary regardless of what it would really load.
  #
  # Rewriting the driver's specs makes the default correct rather than
  # leaving every consumer to remember -Wl,--dynamic-linker=. A
  # compiler that emits non-hermetic output unless invoked just so is a
  # trap, and the whole stack passes through this one place.
  def install_hermetic_specs(install, gcc_ver)

    gcc = "#{install}/bin/gcc"
    loader = stack_loader(gcc_ver)

    if !File.exist?(loader)
      error "no hermetic loader at #{loader}: this stack has no glibc, " \
            "which should have been built as a dependency"
      return false
    end

    # -print-file-name=specs is no good here: for a file that does not
    # exist yet — which is always, since we are about to create it —
    # gcc echoes the bare name back. The "install:" line of
    # -print-search-dirs is the directory it actually looks in.
    dirs = `#{gcc} -print-search-dirs`
    m = dirs.match(/^install:\s*(.+)$/)

    if m.nil?
      error "gcc -print-search-dirs has no install: line, so there is " \
            "nowhere to put the specs file"
      return false
    end

    specs_path = File.join(m[1].strip, "specs")

    specs = `#{gcc} -dumpspecs`
    if !specs.include?(SYSTEM_LOADER)
      error "gcc's link spec does not mention #{SYSTEM_LOADER}: the " \
            "hardcoded interpreter has moved and this rewrite would " \
            "silently do nothing"
      return false
    end

    specs = specs.gsub(SYSTEM_LOADER, loader)

    specs = add_link_rpath(specs,
                           "#{pkgmgr.hermetic_sysroot(gcc_ver)}/usr/lib")
    if specs.nil?
      error "gcc -dumpspecs has no *link: section to add an rpath to"
      return false
    end

    FileUtils.mkdir_p(File.dirname(specs_path))
    File.write(specs_path, specs)
    info "Hermetic specs installed: interpreter -> #{loader}"
    return true
  end

  # Record the library search path in the binaries themselves.
  #
  # Without this, hermeticity is an accident of which loader happens to
  # run: our ld.so has the sysroot compiled in as its default search
  # path, so it finds our libraries, and the SYSTEM ld.so finds the
  # system's. The binary says nothing either way. That is not a
  # theoretical difference —
  #
  #   $ LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu our-loader --list prog
  #     libstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6
  #     libc.so.6      => /usr/lib/x86_64-linux-gnu/libc.so.6
  #
  # — one environment variable and our own loader loads the system's
  # libraries.
  #
  # DT_RPATH rather than DT_RUNPATH, because RPATH is searched BEFORE
  # LD_LIBRARY_PATH and RUNPATH after it; only the former is immune.
  # --disable-new-dtags is our binutils' default today, but that is a
  # build-time default of binutils rather than a promise, and silently
  # getting RUNPATH would silently restore the hole.
  def add_link_rpath(specs, libdir)

    marker = "*link:\n"
    i = specs.index(marker)
    return nil if i.nil?

    body_start = i + marker.length
    body_end = specs.index("\n", body_start)
    return nil if body_end.nil?

    body = specs[body_start...body_end]
    added = " %{!static:-rpath #{libdir} --disable-new-dtags}"

    return specs[0...body_start] + body + added + specs[body_end..]
  end

  # Make the compiler prove itself before the install is accepted.
  #
  # Everything above this point is a claim about how GCC was
  # configured; this is the only step that observes what it actually
  # produces. It is cheap, and a toolchain quietly emitting
  # system-linked binaries would poison every package built after it.
  def verify_produces_hermetic_binaries(install, gcc_ver)

    ok = false

    Dir.mktmpdir("gcc-hermetic-check-") do |d|
      src = File.join(d, "t.c")
      bin = File.join(d, "t")
      File.write(src, "int main(void){return 0;}\n")

      if !system("#{install}/bin/gcc", "-O0", "-o", bin, src,
                 out: File::NULL, err: File::NULL)
        error "the installed gcc cannot compile a trivial program"
        next
      end

      loader = stack_loader(gcc_ver)
      refs = Hermeticity.read_refs(bin, readelf: "#{binutils_bin_dir}/readelf")
      resolved = Hermeticity.resolve_libs(bin, loader: loader)

      violations = Hermeticity.check_refs(
        bin, interp: refs[:interp], rpaths: refs[:rpaths],
        resolved: resolved || {}, allowed: [TC]
      )

      if !violations.empty?
        error "gcc #{gcc_ver} produces non-hermetic binaries:"
        violations.each { |v| error "  #{v.kind}: #{v.detail}" }
        next
      end

      info "Verified: gcc produces hermetic binaries by default"
      ok = true
    end

    return ok
  end

  # Where our as and ld live. Resolved through the package rather than
  # the sysroot: binutils is a :distro package and deliberately not
  # part of the sysroot at all.
  def binutils_bin_dir
    pkg = pkgmgr.get("host_binutils")
    return pkg.install_prefix(pkg.default_ver) / "install" / "bin"
  end
end

pkgmgr.register(HostGccPackage.new())
