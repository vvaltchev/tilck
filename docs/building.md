
Building & configuring Tilck
-------------------------------------------------

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

#### Extra packages
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
`make` or `make -j` there. That means that **out-of-tree builds** are supported
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

## Configuring Tilck

When `cmake_run` is run, all configuration options are dumped on the screen as
CMake info messages. Therefore, changing one of them is easy as running:

    ./scripts/cmake_run -DDEBUG_CHECKS=0

(Note: `cmake_run` forwards its arguments to CMake.) But, that's certainly not
the best way to (re)configure Tilck. A more convenient way is to use CMake's
console tool called `ccmake`, part of `cmake-curses-gui` package (at least on
Debian systems). Its main advantage is that all the options are visible and
editable in an interactive way and that for each option there is a description.
It's not visually cool and fancy like Linux's kconfig, but it's fully integrated
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
the its legacy bootloader. But, when some work has to be done on the UEFI
bootloader, it's very convenient (and quicker) to test it on a VM, before
running it on real hardware. To do that, it's necessary to first to ask the
`build_toolchain` script to download the Open Virtual Machine Firmware ([OVMF])
with:

    ./scripts/build_toolchian -s download_ovmf

After that, it will be possible to boot Tilck by running:

    ./build/run_efi_qemu32

Note: there's a `run_efi_qemu64` script as well. Its purpose is to test the
very realistic case where modern x86_64 machines run Tilck and the UEFI
bootloader has to switch from `long mode` to `protected mode 32` before jumping
to the kernel.

[OVMF]: https://github.com/tianocore/tianocore.github.io/wiki/OVMF

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

    make -j gtests

To run them, execute:

    ./build/gtests

[googletest]: https://github.com/google/googletest
