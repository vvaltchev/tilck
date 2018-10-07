Tilck (Tiny Linux-Compatible Kernel)
-------------------------------------

![TravisCI](https://travis-ci.org/vvaltchev/tilck.svg?branch=master)
![CircleCI](https://circleci.com/gh/vvaltchev/tilck.svg?style=svg)

![Tilck screenshot](other/screenshot.png?raw=true "Tilck")

What is Tilck?
----------------------------------------

Born as a purely educational project called *ExperimentOS*, `Tilck` is evolving as
a tiny *monolithic* x86 Linux compatible kernel targeting (in the very long term)
production embedded systems, while still remaining attached to the educational
world: project's small-scale makes it suitable for operating system research
projects. While it is just one of the many "small kernels" existing today, it is
probably the only one (?) *designed* to support natively x86 Linux binaries. This
**key-feature** allows it to rapidly been able to support a considerable amount of
real-world programs compiled for Linux. The project however, does **not** (and
never will) target to replace Linux: its goal is to offer anyone the possibility to
build an extremely *customizable* and *deterministic* embedded system, while using
a friendly and well-known (kernel - userspace) interface, the Linux one. Also, it
is not part of project's goals to re-implement completely the Linux interface (all
the syscalls, everything in /proc etc.) but to support just the minimum necessary
in order to run a class of Linux console applications. The whole point is having a
system as simple to build and test as an *unikernel*, while preserving many of the
features offered by traditional operating systems. Another **key-feature** of
`Tilck` is its permissive license: when at some point in the future the kernel will
be ready for production systems, its license will allow companies to use and modify
it free-of-charge, without having to release the source code of their changes.


Current state of the kernel
----------------------------------------

Today that project is still to some extent educational and **far** from being ready
for any kind of production use, but it is growing very fast with major patch series
being merged once or twice at week. It has a read-only support to `FAT32` ramdisk,
and it can run a discrete amount of `busybox` applications compiled for embedded
Linux. Also, it has a console (supporting both text-mode and framebuffer) which
understands all the essential escape sequences supported by the Linux console: that
allows line-editing and simple `ncurses` applications to work. Finally, the kernel
supports Linux applications using the framebuffer.

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
runs smoothly both on Linux and on Tilck without any noticeable difference.

[supported Linux syscalls]: https://github.com/vvaltchev/tilck/wiki/Linux-syscalls-support-status
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

    qemu-system-i386 -kernel ./build/elf_kernel_stripped -initrd ./build/fatpart

Actually that way of booting the kernel is used in the system tests. A shortcut
for it is:

    ./build/run_multiboot_qemu -initrd ./build/fatpart

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
to build the googletest framework with:

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
kernel. For example, `Tilck` will probably **never** support:

* Swap
* I/O cache
* SMP
* Authentication
* Permissions (except ro/rw attributes)

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
  protected mode: it order to have a full understanding of its complexity, I
  thought it was better to start first with its legacy.

* The `long mode` does not have a full support for the segmentation. I wanted to
  get confident with this technology.

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
