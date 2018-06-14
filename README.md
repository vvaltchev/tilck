exOS
------

[![Build Status](https://travis-ci.org/vvaltchev/experimentOs.svg?branch=master)](https://travis-ci.org/vvaltchev/experimentOs)


**A Linux-compatible x86 kernel written for educational purposes and fun**

![Alt text](other/screenshot.png?raw=true "exOS")

From the technical point of view, the goal of this project is writing a kernel
being able to run *natively* x86 Linux console applications (like shells, text
editors and compilers), without having to rebuild any part of them. The kernel
will support only a subset of today's Linux syscalls but that subset will be
determinied by the minimum necessary to run a given set of applications.
Briefly, making some typical console programs that are usually embedded in
`busybox` to work correctly, is a *must*. In addition to that, being able run
console editors and compilers will be a nice perk. At the moment, it is not part
of project's goals the kernel to actually have disk drivers: everything is
supposed to be in-memory.

Once the main goal is achieved, this simple kernel could be actually used for
any kind of kernel-development *experiments* with the advantage that changes
will be *orders of magnitude* simpler to implement in **exOS** compared to doing
that in a world-class production-quality kernel like Linux. Also, this project
may allow anyone interested in kernel development to see how a minimalistic
linux-compatible kernel can be written by just looking at its commits, from the
first lines of its bootloader to the implementation of its most complex syscalls
including, along the way, the story of all of its defects and fixes.

Current state: the kernel
----------------------------------------

Currently, the project is growing very fast with major patch series being merged
once or twice at week. Therefore, keeping an accurate track of kernel's features
and capabilities in this section is too time-consuming at the moment. In order to
get an idea of what it can do, just take a look at its code and try it.
Building it takes less than 1 minute (~5 minutes if we consider also running
scripts/build_toolchain).

Current state: the legacy bootloader
--------------------------------------

`exOS` includes a 3-stage bootloader able to load in memory the contents of the
boot-drive at a pre-defined physical address. In its 3rd stage (written in C),
the bootloader loads from the (now in-memory) FAT32 partition the ELF kernel of
`exOS` [it understands the ELF format] and jumps to its entry-point.

Current state: the UEFI bootloader
------------------------------------

`exOS` includes also a fully-working EFI bootloader which boots the kernel in
graphics mode. [Yes, the kernel has a nice graphics console as well.]


Hardware support
--------------------

From the beginning of its development, `exOS` has been tested both on
virtualized hardware (`qemu`, `virtualbox`, `vmware workstation`) and on several
hardware machines. Therefore, `exOS` should work on any `i686+` machine
compatible with the IBM-PC architecture, supporting the PSE (page-size extension)
feature (introduced in Pentium Pro, 1995). If you want to try it, just use `dd`
to store `exos.img` in a flash drive and than use it for booting.

How to build & run
---------------------

Step 0. Enter project's root directory.

Step 1. Build the toolchain by running:

    ./scripts/build_toolchian

Step 2. Compile the kernel and prepare the bootable image with just:

    make -j

Step 3. Now you should have an image file named `exos.img` in the `build`
directory. The easiest way for actually trying `exOS` at that point is to run:

    ./build/run_qemu

NOTE: in case your kernel doesn't have KVM support for any reason, you can
always run QEMU using full-software virtualization:

    ./build/run_nokvm_qemu

Step 4.

    Enjoy :-)


How to build & run (UEFI boot with graphics console)
------------------------------------------------------

Step 0: as above

Step 1. Build the toolchain by running:

    ./scripts/build_toolchian

Step 2. Download OVMF (not downloaded by default)

    ./scripts/build_toolchian -s download_ovmf

Step 3. Build the kernel and the image using a GPT partition table

    make -j gpt_image

Step 4. Run QEMU using the OVMF firmware

    ./build/run_efi_qemu32

NOTE: in case you cannot use KVM:

    ./build/run_efi_nokvm_qemu32

Step 5.

    Enjoy :-)


Unit tests
-------------

You could build kernel's unit tests this way:

    make -j gtests

And run them with:

    ./build/gtests

System tests
--------------

You can run kernel's system tests this way:

    ./build/st/run_all_tests

NOTE: in order the script to work, you need to have python 2.7.x installed as
/usr/bin/python.


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






