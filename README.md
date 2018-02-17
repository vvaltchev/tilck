exOS
------

[![Build Status](https://travis-ci.org/vvaltchev/experimentOs.svg?branch=master)](https://travis-ci.org/vvaltchev/experimentOs)



A Linux-compatible x86 kernel written for educational purposes and fun.

From the technical point of view, the goal of this project is writing a kernel being able to run *natively* x86 Linux console applications (like shells, text editors and compilers), without having to rebuild any part of them. The kernel will support only a subset of today's Linux syscalls but that subset will be determinied by the minimum necessary to run a given set of applications. Briefly, making typical applications like `bash`, `ls`, `cat`, `sed`, `vi`, `gcc` to work correctly, is a *must*. At the moment, it is not part of project's main goal the kernel to actually have disk drivers, nor graphic ones: everything will be loaded at boot time and only a ram disk will be available. At most, maybe a simple network driver will be implemented or an actual driver will be ported from an open-source kernel (Linux or FreeBSD).

Once the main goal is achieved, this simple kernel could be actually used for any kind of kernel-development *experiments* with the advantage that changes will be *orders of magnitude* simpler to implement in **exOS** compared to doing that in a world-class production-quality kernel like Linux. Also, this project may allow anyone interested in kernel development to see how a minimalistic linux-compatible kernel can be written by just looking at its commits, from the first lines of its bootloader to the implementation of its most complex syscalls including, along the way, the story of all of its defects and fixes.

Current state: the kernel
--------------------------

At the moment, exOS is a primitive monolithic non-SMP kernel supporting usermode (single-threaded) processes with dedicated kernel stack, some Linux "int 0x80" syscalls, `fork` with copy-on-write pages, preemptable kernel threads, kernel tasklets, nested interrupts, basic kernel synchronization primitives like mutexes and condition variables and other still demo-level things a PS/2 keyboard driver that just prints the input and a term driver with scrolling support. Concepts like char/block devices have not been implemented yet: that's why the term and the KB "drivers" are left in a demo-like state, for now.
From the point of view of user applications, the kernel is able to load statically linked (Linux) ELF binaries and to deal with some basic syscalls like `fork`, `exit`, `getpid` and `waitpid` (incomplete) plus a demo version of `write` that just writes everything to the console. The usermode binaries at the moment must be compiled with `dietlibc` in order to work since other implementations of libc like `glibc` require some advanced TLS-related syscalls (like `set_thread_area`) to work during the initialization: the support for those syscalls will be introduced later in exOS.

The only usermode program loaded by the kernel by now is what will be the actual `init` process (today just a test program). The program is loaded by reading `/sbin/init` from a memory-mapped FAT32 partition, loaded by the custom bootloader, part of this project.

Current state: the bootloader(s)
---------------------------------

`exOS` includes a 3-stage bootloader able to load in memory the contents of the boot-drive at a pre-defined
physical address. In its 3rd stage (written in C), the bootloader loads from the (now in-memory) FAT32
partition the ELF kernel of `exOS` [it understands the ELF format] and jumps to its entry-point.

In addition to that, the project includes an experimental x86-64 EFI bootloader which allows the kernel to
boot using EFI, if a CSM (compatibility support module) is available. The CSM is necessary since the EFI
bootloder switches back to real mode and puts the video card in 'text mode' by using `int 0x10`, before entering 
finally in protected mode and jumping into kernel's entry point. The issue with the EFI boot is that the
nice PS/2 emulation available after a "legacy" boot is not available. Therefore, the keyboard driver actually
does not work.

Hardware support
--------------------

From the beginning of its development, `exOS` has been tested both on virtualized hardware (`qemu`, `virtualbox`, `vmware workstation`) and on bare-metal machines, like my own Dell XPS 13" 9360. Therefore `exOS` should work on any `80486+` machine compatible with the IBM-PC architecture that supports legacy boot. At least 144 MB of RAM are required, because of the way the OS partitions the physical memory. Such requirement may be removed later.
If you want to try it, just use `dd` to store `exos.img` in a flash drive and than use it for booting.

How to build & run
---------------------

Step 0. Enter project's root directory.

Step 1. Build the toolchain by running:

    ./scripts/build_toolchian

Step 2. Compile the kernel and prepare the bootable image with just:

    make

Step 4. Now you should have an image file named `exos.img` in the `build` directory.
The easiest way for actually trying `exOS` at that point is to just run:

    ./run_qemu

Step 5. Enjoy :-)

Unit tests
-------------

You could build kernel's unit tests this way:

    make gtests

And run them with:

    ./build/gtests



FAQ (by vvaltchev)
---------------------


#### Why many commit messages are so short and incomplete?

It is well-known that all of the popular open source projects care about having good commit messages.
It is an investment that pays off. I even wrote a [blog post](https://blogs.vmware.com/opensource/2017/12/28/open-source-proprietary-software-engineer/) about that.
The problem is that such investment actually starts paying off only when multiple people contribute to the project.
Even in the case of small teams (2 people) it not obvious that it is worth spending hours in re-ordering and editing all the commits of
a pull request until its *story* is perfect, especially when the project is not mature enough: the commits in a pull request have to be just *good enough* in terms of commit message, scope of the change, relative order etc. The focus is on shape of the code *after* the patch series in the sense that limited hacks in the middle of a series are allowed. Clearly, that does **not** mean that commit messages like some of the current ones will be acceptable: as a second contributor comes in, the commit messages need necessarily to become more descriptive, in order to allow the collaboration to work. But, at this stage, going as fast as possible towards the first milestone makes sense. Still, I'm trying to keep the length of the commit messages proportionate to the complexity of the change. Sometimes, even in this stage, it makes sense to spend some time on describing the reasoning behind a commit. As the projects grows, I'll be spending more and more time on writing better commit messages.


#### Why exOS does not have the abstraction XYZ like other kernels do?

`exOS` is **not** meant to be a full-featured production kernel. Please, refer to Linux for that.
The idea is implementing the simplest kernel able to run a class of Linux console applications.
After that, eventually, it can support more advanced stuff like USB mass storage devices,
but not necessarily along with all the powerful features that Linux offers.
The whole point of supporting something is because it is interesting for me (or other contributors)
to learn how it works and to write a proof-of-concept implementation.


#### Why using FAT32?

Actually FAT32 is not a very good choice for any UNIX system.
I decided to use it because it was the simplest widely-used filesystem I knew and I did not want to spend to much time implementing support for a more sophisticated filesystem like `ext2`, at least not initially. Also, I was just curious about learning it. Afterall, the whole point of `experimentOs`, is *experimenting*.
A decision to implement something can be driven just by curiosity and desire to learn a given technology. There is no strict roadmap to follow. The only important goal after learning and having fun is to make `exOS` capable of running a given set of console applications. Ideally, most of the ones in `busybox`. Anyway, exOS just need a filesystem, any filesystem. The biggest issue with FAT32 is that it does not support symlinks. Probably I'll implement a hack like `umsdos` did to support them.






