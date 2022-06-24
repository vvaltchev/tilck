
# Building & configuring Tilck

## Contents

  * [Introduction](#introduction)
  * [Building Tilck's toolchain](#building-tilcks-toolchain)
    - [Extra packages](#extra-packages)
  * [Building Tilck](#building-tilck)
  * [Configuring Tilck](#configuring-tilck)
  * [Build types](#build-types)
  * [Running the UEFI bootloader on QEMU](#running-the-uefi-bootloader-on-qemu)
  * [Building Tilck's unit tests](#building-tilcks-unit-tests)
  * [Building Tilck with Clang (advanced)](#building-tilck-with-clang-advanced)
  * [Special build configurations (advanced)](#special-build-configurations-advanced)

## Introduction

Tilck has a CMake-based build system relying on a local toolchain, built by the
`build_toolchain` Bash script. The first time Tilck's repo is cloned, it is
necessary to build Tilck's local toolchain by running that script. After that,
it will be possible to prepare Tilck's build by running CMake in any chosen
build directory. While running `cmake` directly it is possible, the strongly
recommended practice in Tilck is to run the wrapper script `cmake_run` instead.
That script checks the version of the GCC compiler on the machine, offers handy
shortcuts, allows us to use a CMake binary from the local toolchain, and other
things.

## Building Tilck's toolchain

When run without any options, the `build_toolchain` script installs a minimal
set of packages (e.g. cross GCC toolchain, busybox, etc.) in the local toolchain.
While it's part of Tilck's philosophy to have as fewer dependencies as possible,
there still are some packages required to be installed at *system level*
(e.g. gcc, git, wget, tar, grep, make, etc.) simply because they're needed to
build Tilck's toolchain itself. Once run, the `build_toolchain` script will take
care of detecting which packages need to be installed and it will also run the
right command for your Linux distribution to install them (e.g.
`sudo apt install gcc g++ git [...]`). Most of the mainstream Linux distributions
are supported. In the case you're using a distro not supported, the script will
just dump a list of programs that need to be installed manually on the system
before the script could continue further.

### Extra packages
After the first run of `build_toolchain` finishes, it's possible to build Tilck
but, that doesn't mean the script becomes useless. Actually, most of the
packages that it can install are not installed by default. The idea behind that
is to perform the first setup as quickly as possible. To see all the packages
available, just run the script with `-h`. Single packages can be installed using
the `-s` option. To be more precise, the script calls them *functions* because
in some cases (e.g. config_busybx) they are meant to just (re)configure a
package. **Shortcut**: while the script lists its functions with their full
name, it accepts also short names like `gtest` instead of `build_gtest` and
`ovmf` instead of `download_ovmf`.

## Building Tilck

To build Tilck, it's necessary to run first `<TILCK_DIR>/scripts/cmake_run` in
the chosen build directory (e.g. `~john/builds/tilck01`) and then just run
`make` there. That means that **out-of-tree builds** are supported
effortlessly.

But, because 99% of the time it's just fine to have a `build` directory inside
our project's main directory (e.g. `~john/devel/tilck`), we can simple run
`./scripts/cmake_run` there. It will detect that's our main directory and it
will create the `build` subdir. After that, we'll still have to enter the
`build` directory and run `make` there, because that's where CMake placed its
generated makefiles. In reality, there's a more convenient **shortcut**: just
run `make` in project's main directory, using the trivial wrapper Makefile
included in Tilck's source: it will simply run `$(MAKE) -C build`. In case the
`build/` directory does not exist yet, it will also run the `cmake_run` wrapper
script first.

#### Build performance notes

To speed up Tilck's build, it's possible to increase the level of parallelism
with GNU make's option `-j<N>`. For example, `make -j4` means that at most
4 instances of the compiler will be run in parallel. **WARNING**: increasing
the level of parallelism requires significantly more memory in the host system
It's hard to estimate in general how much memory each instance of GCC or Clang
will use for compiling a C or C++ file, but it makes sense to be conservative
and to account for ~1 GB of RAM per GCC/Clang instance. Therefore, `make -j4`
would require at least 4 GB of available memory in the host system used for
building Tilck. Still, it's possible to increase the level of parallelism at
your own discretion, keeping in mind that in case the Linux host system runs
out of memory while building this project (or anything else!), the OOM killer
will run and **that will make the system potentially unstable/unusable**.

**See:**
   * Issue https://github.com/vvaltchev/tilck/issues/101
   * https://unix.stackexchange.com/questions/316644/
   * https://unix.stackexchange.com/questions/208568/
   * https://unix.stackexchange.com/questions/153585/
   * https://lwn.net/Articles/317814/

## Configuring Tilck

When `cmake_run` is run, all configuration options are dumped on the screen as
CMake info messages. Therefore, changing one of them is easy as running:

    ./scripts/cmake_run -DDEBUG_CHECKS=0

(Note: `cmake_run` forwards its arguments to CMake.) But, that's certainly not
the best way to (re)configure Tilck. A more convenient way is to use CMake's
console tool called `ccmake`, part of `cmake-curses-gui` package (at least on
Debian systems). Its main advantage is that all the options are visible and
editable in an interactive way and that for each option there is a description.
It's not visually cool and fancy like Linux's Kconfig, but it's fully integrated
with CMake and does a good job, overall. (Maybe in the future Tilck will switch
to Kconfig or some custom tool for a better user experience even in this case.)

Running `ccmake` is simple as:

    ccmake ./build

But, as for the `cmake_run` case, it's **highly recommended** to run its wrapper
script called `run_config`:

    ./scripts/run_config

It has additional error-checking and prevents trivial errors like running
`ccmake .` (which will create an empty CMakeCache.txt in `.`) instead of
`ccmake build/` in project's main directory. Of course, it supports out-of-tree
builds as well: just pass the build directory as first argument.

Finally, there is also a wrapper for people used with Kconfig too, working when
the in-tree build directory is used (again, 99% of the time):

    make config

Or:

    make menuconfig

They have both the same effect as running `run_config`. In summary, it's
convenient to use `run_config` or `make config` instead of running `ccmake`
directly because of its error-checking and because at some point Tilck will
have something with a better UI than `ccmake` that will be run by the wrapper
scripts.

## Build types

Tilck's build types differ only by the optimization flags used. Three build
types are currently supported:

   * Debug: `-O0 -fno-inline-functions`
   * Release: `-O3`
   * MinSizeRel: `-Os`

Tilck's default build type is: Debug. It's worth noting that very few projects
are built by default with `-O0 -fno-inline-functions` because the code produced
is very inefficient. Fortunately, Tilck still performs very well in this case.
Therefore, it's convenient to use this build type in particular when debugging
Tilck because the value of all local variables will be visible in GDB
(no optimizations, no inlining).

Changing Tilck's build type is simple as running:

    ./scripts/cmake_run -DCMAKE_BUILD_TYPE=Release

For `Release` builds there's a nice shortcut:

    REL=1 ./scripts/cmake_run

Finally, it's necessary to remark that, because level of compiler's optimization
is the **only** thing that distinguishes the build types, ASSERTs are checked no
matter the build type. To compile them out, it's necessary to turn off the
`DEBUG_CHECKS` flag.

## Running the UEFI bootloader on QEMU

While Tilck's image is bootable on a UEFI machine, QEMU doesn't by default get
installed with a UEFI firmware. Therefore, typically we boot Tilck using only
its legacy bootloader. But, when some work has to be done on the UEFI bootloader,
it's very convenient (and quicker) to test it on a VM, before running it on real
hardware. To do that, it's necessary to first to ask the `build_toolchain` script
to download the Open Virtual Machine Firmware ([OVMF]) with:

    ./scripts/build_toolchian -s download_ovmf

After that, it will be possible to boot Tilck by running:

    ./build/run_efi_qemu32

Note: there's a `run_efi_qemu64` script as well. Its purpose is to test the
very realistic case where modern x86_64 machines run Tilck and the UEFI
bootloader has to switch from `long mode` to `protected mode 32` before jumping
to the kernel.

## Building Tilck's unit tests

Tilck uses the [googletest] framework for unit tests and that's not downloaded
by `build_toolchian` when it's run without arguments. In order to install that
framework in the toolchain, just run:

    ./scripts/build_toolchian -s gtest

After that, you'll need to run the `cmake_run` script again:

    ./scripts/cmake_run                # For in-tree builds

Or:

    ./scripts/cmake_run <BUILD_DIR>    # For out-of-tree builds

Finally, just build the unit tests with:

    make gtests

To run them, execute:

    ./build/gtests

**Note**: there's more about the unit tests, including special build configurations.
For all of that, see the [testing] document.

## Building Tilck with system's compiler (advanced)
While for most of the projects it makes sense to build by default with system's
compiler and require some special setup in order to work with a cross-compiler,
for Tilck is exactly the opposite. Tilck's build system has been designed around
the idea of using by default a pre-built toolchain from [bootlin.com] for building
the kernel, the boot loaders, and Tilck's user apps (the system compiler is used for
anything else that has to run on the host machine instead). That is extremely convenient
because it saves a lot of time and disk space to anyone who wants to build Tilck
(those pre-built toolchains take just ~60 MB of space) and it's also versatile
because porting Tilck to a new architecture would be trivial, from the point of
view of the build system. Still, in some rare cases using the system's compiler
is helpful. For example, when we want to debug an application to understand what
happens at libc level (Tilck uses [libmusl] as libc), we have to build [libmusl]
and use the system compiler instead of the one from [bootlin.com] because we need
all the debug symbols etc. To do that, first check that:

   - You have a GCC installed on your system

   - Your GCC version can build i686 binaries. On all the systems in the Debian
     family (like Ubuntu, Mint etc.), install the `gcc-multilib` and `g++-multilib`
     packages for that.

Then, install [libmusl] in the toolchain with:

    ./scripts/build_toolchain -s build_libmusl

After that, remove the `build` directory and run `cmake_run` this way:

    USE_SYSCC=1 ./scripts/cmake_run

In case there are multiple versions of GCC installed on the system (e.g. gcc-7 and
gcc-9), also the `CC` and `CXX` variables can be set. **Warning**: being this an
advanced use case, the user should have some confidence with build systems, cross
compilers (etc.) in order to deal with any eventual issues on his/her particular
system, in particular if it's not a *mainstream* Linux distribution. While the
default build of Tilck is widely supported and it has very few external dependencies
(it uses the same pre-built toolchain), in this case system's configuration matters.
Still, Tilck's `build_toolchain` script and the build system are regularly tested
on the following Linux distributions:

   * Ubuntu (primary)
   * Debian
   * Mint
   * Fedora
   * Arch
   * Manjaro
   * openSUSE

[bootlin.com]: https://toolchains.bootlin.com
[libmusl]: https://www.musl-libc.org/

## Building Tilck with Clang (advanced)
The Tilck project **cannot** be entirely built with a Clang toolchain, at the
moment. But, it still has partial support for Clang because:

   * Supporting more than compiler (in general) improves the code

   * Clang sometimes reports warnings in cases where GCC doesn't (the opposite
     is true as well)

   * The `-Wconversion` flag as implemented in Clang is very useful

   * Clang's [Static Analyzer] is great

   * Tilck has a custom plug-in for the Clang's [Static Analyzer] (but currently
     it's not open source)

#### How to build with Clang

   1. Install `clang` and `clang++` and make sure it can build i686 binaries.
   2. Run: `CC=clang CXX=clang++ ./scripts/cmake_run -DKERNEL_SYSCC=1`
   3. Build as always with `make`

The `KERNEL_SYSCC` option makes the build system to use `CC` and `CXX` to build
kernel's C and C++ files, while still building the assembly files with the
pre-built GCC toolchain from [bootlin.com]. **Note**: no matter the
`KERNEL_SYSCC` option, kernel's unit tests (see [testing]) are always built
using system's compiler. Therefore, setting `CC=clang CXX=clang++` will cause
the unit tests to be completely built with Clang: this scenario is fully
supported and has no tricky limitations.

#### Why we can't build the whole project with Clang?

   1. Major problem: `llvm-as` is not 100% compatible with `gas`. In particular,
      it does not support expressions as literals (e.g. 0x1000 + 32 * 8) in the
      same way `gas` does.

   2. Minor problem: Clang does not support the Microsoft ABI when the output's
      type is not a [PE] binary, while GCC does not support [PE] as output at
      all. That prevents using Clang to build the EFI bootloader. A dedicated
      build of the EFI bootloader for Clang *can* be written, but it would be
      very different from the GCC one but, supporting two completely different
      builds for the same target is an overkill, especially because it wouldn't
      be enough for building *everything* with Clang (see the problem above).


## Special build configurations (advanced)
Tilck has a predefined list of supported configurations which must always build
and pass all the tests. Most of them are built & run each time a branch is pushed
by the [Azure Pipelines] CI but some of them are left out because of the limited
resources available there. Anyway, independently of the amount of resources available
in CI builds, we need a simple way to test everything on our local machines.
That's why the directory `scripts/build_generators` has been created. Each file
there is shell script which creates a different configuration by calling
`cmake_run` with a specific set of options. On top of that, there's the
`./scripts/adv/gen_other_builds` script which actually *builds* the kernel and
its unit tests for each configuration, and finally, there's (in the same directory)
the `test_all_other_builds` script which *runs* all the tests (except the interactive
ones) for each configuration. **Note**: these scripts expect a variety of additional
packages to be installed both on the host machine and in Tilck's toolchain. Before
running them for the first time, check (following the sections above) that the
whole project can build with the system's compiler (GCC) and that the kernel and
the unit tests can build with Clang as well because both of those build
configurations are used.


[testing]: testing.md
[googletest]: https://github.com/google/googletest
[Azure Pipelines]: https://azure.microsoft.com/en-us/services/devops/pipelines
[Static Analyzer]: https://clang-analyzer.llvm.org/
[OVMF]: https://github.com/tianocore/tianocore.github.io/wiki/OVMF
[PE]: https://en.wikipedia.org/wiki/Portable_Executable
