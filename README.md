Tilck (Tiny Linux-Compatible Kernel)
-------------------------------------

[![CircleCI](https://circleci.com/gh/vvaltchev/tilck.svg?style=svg)](https://circleci.com/gh/vvaltchev/tilck)
[![TravisCI](https://travis-ci.org/vvaltchev/tilck.svg?branch=master)](https://travis-ci.org/vvaltchev/tilck)
[![codecov](https://codecov.io/gh/vvaltchev/tilck/branch/master/graph/badge.svg)](https://codecov.io/gh/vvaltchev/tilck)
[![License](https://img.shields.io/badge/License-BSD%202--Clause-orange.svg)](https://opensource.org/licenses/BSD-2-Clause)

![Tilck screenshot](other/screenshot.png?raw=true "Tilck")

What is Tilck?
----------------------------------------

`Tilck` is an educational *monolithic* x86 kernel designed to be Linux-compatible at
binary level. Project's small-scale and intentional simplification makes it suitable
people who want to learn kernel development and play with a simple Linux-like system
and, maybe, for some particular operating system research projects where a huge code
base (like the Linux kernel) might be an obstacle for implementing a new idea as a
proof-of-concept or at prototype level. In addition to that, in the long term,
`Tilck` might become suitable for **embedded systems** where a kernel as simple as
possible is required or, at least, it is considered the optimal solution. One can
think about `Tilck` as a kernel as simple to build and customize as a *unikernel*,
but which offers a fair amount of features typically provided by traditional
operating systems. As a consequence of that, the development of `Tilck` will answer
the following two questions:

> 1) How simple could possibly be a kernel able to run a fair-amount of useful Linux
> console programs?

> 2) How fast a given syscall / kernel subsystem can get if we get rid of its most
> complex features?

While as per today `Tilck` is still far from being able to provide some answers
to the first question, some progress has been made with the second one: as shown
in this [article], thanks to several simplifications, `Tilck`'s console is
faster than the Linux one.

What Tilck is NOT ?
----------------------------------------

An attempt to re-write and/or replace the Linux kernel. Tilck is a completely
different kernel that has a *partial* compatibility with Linux just in order to
take advantage of the programs (and toolchains) already written for it. But,
having a fair amount of Linux programs working on it is just a starting point.
After that, Tilck will evolve in a different way and it will have its own unique
set of features. Tilck is fundamentally different from Linux in its design and
its trade-offs as it **does not** aim to target multi-user server or
desktop-class machines.

[article]: https://github.com/vvaltchev/tilck/wiki/Getting-performance-through-simplification:-Tilck's-console

Current state of the kernel
----------------------------------------

Today that project is **far** from being ready for any kind of production use, but
it is growing very fast with major patch series being merged once or twice at week.
It has a read-only support to `FAT32` ramdisk, and it can run a discrete amount of
`busybox` applications compiled for embedded Linux. Also, it has a console
(supporting both text-mode and framebuffer) which understands all the essential
escape sequences supported by the Linux console: that allows line-editing and
simple `ncurses` applications to work. Finally, the kernel supports Linux
applications using the framebuffer.

For a slightly more accurate idea of kernel's features, please check the list of
[supported Linux syscalls] or see what `Tilck` can do at any time by building it.
**Note**: the project's build system has been designed to work *effortlessly* on
a variety of Linux distributions and it offers to automatically install (using
distribution's package management system) the missing packages. About that, it's
worth mentioning that it's part of project's philosophy to require as few as
possible packages to be installed on the machine (e.g. `bintools`, `gcc`, `git`,
`wget` etc.): the rest of the required packages are downloaded and compiled in
the `toolchain` directory.

In case of any problems with the build system, please don't hesitate to file an
issue describing your problem.

![Tetris running on Tilck](other/tetris.png?raw=true)

The image here above shows a Tetris implementation based on the [Tiny Framebuffer
Library] running on `Tilck` in a `QEMU` virtual machine. The game's executable,
runs smoothly both on `Linux` and on `Tilck`.

[supported Linux syscalls]: docs/syscalls.md
[Tiny Framebuffer Library]: https://github.com/vvaltchev/tfblib

The legacy bootloader
----------------------------------------

`Tilck` includes a 3-stage multiboot bootloader able to load in memory the
contents of the boot-drive at a pre-defined physical address. In its 3rd stage
(written in C), the bootloader loads from an in-memory `FAT32` partition the ELF
kernel of `Tilck` [it understands the ELF format] and jumps to its entry-point.
Before the final jump to the kernel, the bootloader allows the user the choose
the resolution of a graphics video mode. The VGA text-mode is supported as well.

The UEFI bootloader
----------------------------------------

`Tilck` includes also a fully-working multiboot EFI bootloader which boots the
kernel in graphics mode (text mode is not available when booting using UEFI).
From kernel's point-of-view, the two bootloaders are equivalent.

Other bootloaders
----------------------------------------

`Tilck` can be booted by any bootloader supporting `multiboot 1.0`. For example,
qemu's simple bootloader designed as a shortcut for loading directly the Linux
kernel, without any on-disk bootloaders can perfectly work with `Tilck`:

    qemu-system-i386 -kernel ./build/tilck -initrd ./build/fatpart

Actually that way of booting the kernel is used in the system tests. A shortcut
for it is:

    ./build/run_multiboot_qemu -initrd ./build/fatpart

#### Grub support

`Tilck` can be easily booted with GRUB. Just edit your `/etc/grub.d/40_custom`
file (or create another one) by adding an entry like:

```
menuentry "Tilck" {
    multiboot <PATH-TO-TILCK>/tilck/build/tilck
    module --nounzip <PATH-TO-TILCK>/tilck/build/fatpart
    boot
}
```
After that, just run `update-grub` as root and reboot your machine.

Hardware support
--------------------

From the beginning of its development, `Tilck` has been tested both on virtualized
hardware (`qemu`, `virtualbox`, `vmware workstation`) and on several hardware
machines. Therefore, `Tilck` should work on any `i686+` machine compatible with the
IBM-PC architecture, supporting the PSE (page-size extension) feature (introduced
in Pentium Pro, 1995). If you want to try it, just use `dd` to store `tilck.img` in
a flash drive and than use it for booting.

How to build & run
---------------------

Building the project requires a Linux x86_64 system or Microsoft's
`Windows Subsystem for Linux (WSL)`.

Step 0. Enter project's root directory.

Step 1. Build the toolchain by running:

    ./scripts/build_toolchian

Step 2. Compile the kernel and prepare the bootable image with just:

    make -j

Step 3. Now you should have an image file named `tilck.img` in the `build`
directory. The easiest way for actually trying `Tilck` at that point is to run:

    ./build/run_qemu

**NOTE**: in case your kernel doesn't have `KVM` support for any reason, you can
always run `QEMU` using full-software virtualization:

    ./build/run_nokvm_qemu


How to build & run (UEFI)
------------------------------------------------------

Step 0: as above

Step 1. as above

Step 2. Download `OVMF` (not downloaded by default)

    ./scripts/build_toolchian -s download_ovmf

Step 3. Build the kernel and the image using a GPT partition table

    make -j gpt_image

Step 4. Run QEMU using the OVMF firmware

    ./build/run_efi_qemu32

**NOTE**: in case you cannot use `KVM`:

    ./build/run_efi_nokvm_qemu32

Unit tests
-------------

In order to build kernel's unit tests, it is necessary first
to build the `googletest` framework with:

    ./scripts/build_toolchain -s build_gtest

Then, the tests could be built this way:

    make -j gtests

And run with:

    ./build/gtests

System tests
--------------

You can run kernel's system tests this way:

    ./build/st/run_all_tests

**NOTE**: in order the script to work, you need to have `python` 2.7.x
installed as `/usr/bin/python`.

A comment about user experience
----------------------------------

Tilck particularly distinguishes itself from many open source projects in one
way: it really cares about the **user experience** (where "user" means
"developer"). It's not the typical super-cool low-level project that's insanely
complex to build and configure; it's not a project requiring 200 things to be
installed on the host machine. Building such projects may require hours or even
days of effort (think about special configurations e.g. cross builds). Tilck
instead, has been designed to be trivial to build and test even for students who
can barely use Linux (actually it can even be built and run on QEMU w/o KVM on
Microsoft's WSL, aka "Bash for Windows"). It has a sophisticated script for
building its own toolchain that works for the major Linux distributions and a
powerful CMake-based build system. The build of Tilck, produce an image ready to
be tested with QEMU or written on a USB stick. (To some degree, it's like what
the `buildroot` project does for Linux.) Of course, the project includes also
scripts for running Tilck in QEMU with various configurations (bios boot, efi
boot, direct (multi)boot with QEMU's -kernel option, etc.).

#### Tests
Tilck has **unit tests**, **system tests** and **self tests** all in the same
repository, completely integrated with its build system. In addition, there's
full code coverage support and useful scripts for generating HTML reports
(see the [coverage] guide). Finally, Tilck is fully integrated with `CircleCI`,
which validates each branch with builds and test runs in a variety
of configurations. The integration with `CodeCov` for checking online the
coverage is another nice perk.

#### Motivation
The reason for having the above mentioned features is to offer its users and
potential contributors a really **nice** experience, avoiding any kind of
frustration. Hopefully, even the most experienced engineers will enjoy a zero
effort experience. But it's not all about reducing the frustration. It's also
about _not scaring_ students and junior developers who might be just curious to
see what this project is all about and maybe eager to write a simple program for
it and/or add a couple of printk()'s here and there in their fork. Hopefully,
some of those people "just playing" with Tilck might actually want to contribute
to its development.

In conclusion, even if some parts of the project itself might be pretty complex,
at least building and running its test **must be** something anyone can do.

[coverage]: docs/coverage.md

FAQ (by vvaltchev)
---------------------

#### Why many commit messages are so short and incomplete?

It is well-known that all of the popular open source projects care about having good
commit messages. It is an investment that at some point pays off. I even wrote a
[blog post] about that. The problem is that such investment actually starts paying
off only when multiple people contribute to the project. Even in the case of small
teams (2 people) it not obvious that it is worth spending hours in re-ordering and
editing all the commits of a pull request until its *story* is perfect, especially
when the project is not mature enough: the commits in a pull request have to be just
*good enough* in terms of commit message, scope of the change, relative order etc.
The focus here is on shape of the code *after* the patch series in the sense that
limited hacks in the middle of a series are allowed. As a second contributor comes
in, the commit messages will need necessarily to become more descriptive, in order
to allow the collaboration to work. But, at this stage, going as fast as possible
towards the first milestone makes sense. As the project matures, I'll be spending
more and more time on writing better commit messages.

[blog post]: https://blogs.vmware.com/opensource/2017/12/28/open-source-proprietary-software-engineer

#### How usermode applications are built?

Tilck's build system uses a x86 GCC toolchain based on `libmusl` instead of `glibc`
and static linking in order to compile the user applications running on it. Such
setup is extremely convenient since it allows the same binary to be run directly on
Linux and have its behavior validated as well as it allows a performance comparison
between the two kernels. It is **extremely easy** to build applications for Tilck
outside of its build system. It's enough to download a pre-compiled toolchain from
https://toolchains.bootlin.com/ for `i686` using `libmusl` and just always link
statically.


#### Why Tilck does not have the feature/abstraction XYZ like other kernels do?

`Tilck` is **not** meant to be a full-featured production kernel. Please, refer to
Linux for that. The idea at the moment to implement a kernel as simple as possible
able to run a class of Linux console applications. At some point in the future
`Tilck` might actually have a chance to be used in production embedded environments,
but it still will be *by design* limited in terms of features compared to the Linux
kernel. For example, `Tilck` will *probably* never support:

* Swap
* I/O cache
* SMP
* Multiple users

`Tilck`'s whole purpose is being *simple* and *extremely-deterministic*, while
most of the above-mentioned features introduce a substantial amount of complexity
in a kernel. As mentioned above, one can think of Tilck as a kernel offering what
a unikernel will never be able to offer, but without trying to be a kernel for
full-blown desktop/server systems.

#### Why Tilck runs only on i686?

Actually Tilck runs only on i686 *for the moment*. The kernel was born as a purely
educational project and the x86 architecture was already very friendly to me at
the time. Moving from x86 usermode assembly to "kernel" mode (real-mode and the
transitions back and forth to protected mode for the bootloader) required quite an
effort, but it still was, in my opinion, easier than "jumping" directly into a
completely unknown (for me) architecture, like `ARM`. I've also considered writing
from the beginning a `x86_64` kernel running completely in `long mode` but I
decided to stick initially with the `i686` architecture for the following reasons:

* The `long mode` is, roughly, another "layer" added on the top of 32-bit
  protected mode: in order to have a full understanding of its complexity, I
  thought it was better to start first with its legacy.

* The `long mode` does not have a full support for segmentation, while I wanted
  to get confident with this technology as well.

* The `long mode` has a 4-level paging system, which is more complex to use that
  the classic 2-level paging supported by `ia32` (it was better to start with
  something simpler).

* I never considered the idea of writing a kernel for desktop or server-class
  machines where supporting a huge amount of memory is a must. We already have
  Linux for that.

* It seemed to me at the time, that there was more online "starters" documentation
  (like the articles on https://wiki.osdev.org/) for `i686` compared to any other
  architecture.

Said that, likely I'll make `Tilck` to support also `x86_64` and being able to run
in `long mode` at some point but, given the long-term plans for it as a tiny kernel
for embedded systems, making it to run on `ARM` machines it certainly important.
Unfortunately, the project at the moment is still not developed enough to be
actually useful on its first architecture, therefore spending a lot of effort to
make it run on a second architecture does not make much sense (at this stage).

#### Why using FAT32?

Even if FAT32 is today the only filesystem supported by `Tilck`, in the next months
it will be used only as an initial read-only ramdisk. The main filesystem will be a
custom ramfs, while the FAT32 ramdisk will remain mounted (likely) under /boot. The
#1 reason for using FAT32 was that it is required for booting using UEFI. Therefore,
it was convienent to store there also all the rest of the files.

#### Why using 3 spaces as (soft) tab size?

Long story. It all started after using that coding style for 5 years at VMware.
Initially, it looked pretty weird to me, but at some point I felt in love with
the way my code looked with soft tabs of length=3. I got convinced that 2 spaces
are just not enough, while 4 spaces are somehow "too much". Therefore, when I
started the project in 2016, I decided to stick with tab size I liked more, even
if I totally agree that using 4 spaces would have been better for most of the
people.
