exOS
------

A Linux-compatible x86 kernel written for educational purposes and fun.

From the technical point of view, the goal of this project is writing a kernel being able to run *natively* x86 Linux console applications (like shells, text editors and compilers), without having to rebuild any part of them. The kernel will support only a subset of today's Linux syscalls but that subset will be determinied by the minimum necessary to run a given set of applications. Briefly, making typical applications like `bash`, `ls`, `cat`, `sed`, `vi`, `gcc` to work correctly, is a *must*. At the moment, it is not part of project's main goal the kernel to actually have disk drivers, nor graphic ones: everything will be loaded at boot time and only a ram disk will be available. At most, maybe a simple network driver will be implemented or an actual driver will be ported from an open-source kernel (Linux or FreeBSD).

Once the main goal is achieved, this simple kernel could be actually used for any kind of kernel-development *experiments* with the advantage that changes will be *orders of magnitude* simpler to implement in **exOS** compared to doing that in a world-class production-quality kernel like Linux. Also, this project may allow anyone interested in kernel development to see how a minimalistic linux-compatible kernel can be written by just looking at its commits, from the first lines of its bootloader to the implementation of its most complex syscalls including, along the way, the story of all of its defects and fixes.


Build & run
------------

The project has been designed to be easily buildable on Debian-based systems like Ubuntu, even if it is possible to build the kernel on every Linux system, even on `Bash for Windows 10`.

Step 0. Enter project's root directory.

Step 1. If you're using a Debian-based system, build the toolchain by running:

    ./build_toolchian

In case you're not using a Debian-based system or your system does not have by default `gcc` and `g++` version 6.0 or higher, you'll have to manually setup your environment in a way that such statement is true. For example, if you are using an older Ubuntu can you could:

  * run build_toolchian to install all the necessary packages
  * install gcc-6 and g++-6 somehow
  * add gcc and g++ symlinks in ~/bin
  * add ~/bin as a first entry in your PATH enviroment variable

In case you're not using a Debian system instead, you'll have to take a look at the build_toolchain script and install the same packages using the package manager available on your system.

Step 2. Run cmake this way:

    ./cmake_run

Step 3. Compile the kernel this way:

    ./make_run

Optionally, you could also build kernel's unit tests this way:

    ./make_run tests

Step 4. Now you should have an image file named `exos.img` in the `build` directory.
The easiest way for actually trying `exOS` at that point is to install `qemu` on your system and just run:

    ./run_qemu

Step 5. Enjoy :-)









