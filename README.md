exOS
------

[![Build Status](https://travis-ci.org/vvaltchev/experimentOs.svg?branch=master)](https://travis-ci.org/vvaltchev/experimentOs)



A Linux-compatible x86 kernel written for educational purposes and fun.

From the technical point of view, the goal of this project is writing a kernel being able to run *natively* x86 Linux console applications (like shells, text editors and compilers), without having to rebuild any part of them. The kernel will support only a subset of today's Linux syscalls but that subset will be determinied by the minimum necessary to run a given set of applications. Briefly, making typical applications like `bash`, `ls`, `cat`, `sed`, `vi`, `gcc` to work correctly, is a *must*. At the moment, it is not part of project's main goal the kernel to actually have disk drivers, nor graphic ones: everything will be loaded at boot time and only a ram disk will be available. At most, maybe a simple network driver will be implemented or an actual driver will be ported from an open-source kernel (Linux or FreeBSD).

Once the main goal is achieved, this simple kernel could be actually used for any kind of kernel-development *experiments* with the advantage that changes will be *orders of magnitude* simpler to implement in **exOS** compared to doing that in a world-class production-quality kernel like Linux. Also, this project may allow anyone interested in kernel development to see how a minimalistic linux-compatible kernel can be written by just looking at its commits, from the first lines of its bootloader to the implementation of its most complex syscalls including, along the way, the story of all of its defects and fixes.

Current state
---------------

At the moment, exOS is a primitive monolithic non-SMP kernel supporting usermode (single-threaded) processes with dedicated kernel stack, some Linux "int 0x80" syscalls, `fork` with copy-on-write pages, preemptable kernel threads, kernel tasklets, nested interrupts, basic kernel synchronization primitives like mutexes and condition variables and other still demo-level things a PS/2 keyboard driver that just prints the input and a term driver with scrolling support. Concepts like char/block devices have not been implemented yet: that's why the term and the KB "drivers" are left in a demo-like state, for now.
From the point of view of user applications, the kernel is able to load statically linked (Linux) ELF binaries and to deal with some basic syscalls like `fork`, `exit`, `getpid` and `waitpid` (incomplete) plus a demo version of `write` that just writes everything to the console. The usermode binaries at the moment must be compiled with `dietlibc` in order to work since other implementations of libc like `glibc` require some advanced TLS-related syscalls (like `set_thread_area`) to work during the initialization: the support for those syscalls will be introduced later in exOS.

The only usermode program loaded by the kernel by now is what will be the actual `init` process (today just a test program). The program is loaded by reading `/sbin/init` from a memory-mapped FAT16 partition, loaded by the custom legacy 16-bit x86 bootloader, part of this project.

Hardware support
--------------------

From the beginning of its development, `exOS` has been tested both on virtualized hardware (`qemu`, `virtualbox`, `vmware workstation`) and on bare-metal machines, like my own Dell XPS 13" 9360. Therefore `exOS` should work on any `80486+` machine compatible with the IBM-PC architecture that supports legacy boot. At least 144 MB of RAM are required, because of the way the OS partitions the physical memory. Such requirement may be removed later.
If you want to try it, just use `dd` to store `exos.img` in a flash drive and than use it for booting.

How to build & run
---------------------

The project has been designed to be easily buildable on Debian-based systems like Ubuntu, even if it is possible to build the kernel on every GNU/Linux system.

Step 0. Enter project's root directory.

Step 1. If you're using a Debian-based system, build the toolchain by running:

    ./build_toolchian

In case you're not using a Debian-based system or your system does not have by default `gcc` and `g++` version 6.0 or higher, you'll have to manually setup your environment in a way that such statement is true. For example, if you are using an older Ubuntu can you could:

  * run `build_toolchian` to install most of the necessary packages
  * install `gcc-6` and `g++-6` since the default compiler is older
  * export `CC=gcc-6` and `CXX=g++-6` (at least before running `cmake_run`)
 
In case you're not using a Debian system instead, you'll have to take a look at the `build_toolchain` script and install the same packages using the package manager available on your system.

Step 2. Run `cmake` this way:

    ./cmake_run

Step 3. Compile the kernel and prepare the bootable image this way:

    ./make_run

Step 4. Now you should have an image file named `exos.img` in the `build` directory.
The easiest way for actually trying `exOS` at that point is to just run:

    ./run_qemu

Step 5. Enjoy :-)

Unit tests
-------------

You could build kernel's unit tests this way:

    ./make_run gtests

And run them with:

    ./build/gtests
