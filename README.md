Tilck (Tiny Linux-Compatible Kernel)
-------------------------------------

![TravisCI](https://travis-ci.org/vvaltchev/tilck.svg?branch=master)
![CircleCI](https://circleci.com/gh/vvaltchev/tilck.svg?style=svg)


**A Linux-compatible x86 kernel written for educational purposes and fun**

![Alt text](other/screenshot.png?raw=true "Tilck")

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

Current state: the kernel
----------------------------------------

Currently, the project is growing very fast with major patch series being merged
once or twice at week. Therefore, keeping an accurate track of kernel's features
and capabilities in this section is too time-consuming at the moment. In order
to get an idea of what it can do, just take a look at its code and try it.
Building it takes less than 1 minute (~5 minutes if we consider also running
./scripts/build_toolchain).

The legacy bootloader
----------------------------------------

`Tilck` includes a 3-stage multiboot bootloader able to load in memory the contents
of the boot-drive at a pre-defined physical address. In its 3rd stage (written in C),
the bootloader loads from an in-memory FAT32 partition the ELF kernel of
`Tilck` [it understands the ELF format] and jumps to its entry-point. Before
the final jump to the kernel, the bootloader allows the user the choose the
a video mode among several standard resolutions (graphics mode) + the original
VGA-compatible text mode.

The UEFI bootloader
----------------------------------------

`Tilck` includes also a fully-working multiboot EFI bootloader which boots the kernel
in graphics mode (text mode is not available when booting using UEFI). From kernel's
point-of-view, the two bootloaders are equivalent.

Other bootloaders
----------------------------------------

`Tilck` can be booted by any bootloader supporting multiboot 1.0. For example, qemu's
simple bootloader designed as a shortcut for loading directly the Linux kernel, without
any on-disk bootloaders can perfectly work with `Tilck`:

    qemu-system-i386 -kernel ./build/elf_kernel_stripped -initrd ./build/fatpart
    
Actually that way of booting the kernel is used in the system tests. A shortcut for it is:

    ./build/run_multiboot_qemu -initrd ./build/fatpart

Hardware support
--------------------

From the beginning of its development, `Tilck` has been tested both on
virtualized hardware (`qemu`, `virtualbox`, `vmware workstation`) and on several
hardware machines. Therefore, `Tilck` should work on any `i686+` machine
compatible with the IBM-PC architecture, supporting the PSE (page-size extension)
feature (introduced in Pentium Pro, 1995). If you want to try it, just use `dd`
to store `tilck.img` in a flash drive and than use it for booting.

How to build & run
---------------------

Step 0. Enter project's root directory.

Step 1. Build the toolchain by running:

    ./scripts/build_toolchian

Step 2. Compile the kernel and prepare the bootable image with just:

    make -j

Step 3. Now you should have an image file named `tilck.img` in the `build`
directory. The easiest way for actually trying `Tilck` at that point is to run:

    ./build/run_qemu

NOTE: in case your kernel doesn't have KVM support for any reason, you can
always run QEMU using full-software virtualization:

    ./build/run_nokvm_qemu

Step 4.

    Enjoy :-)


How to build & run (UEFI)
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

NOTE: in order the script to work, you need to have python 2.7.x installed as
/usr/bin/python.

Linux syscalls support status
-------------------------------

`Tilck` supports both the `int 0x80` syscall interface and the `sysenter` one.
Here below there is an up-to-date table containing the status of each supported syscall
at the moment. Because the list of the not-yet-supported syscalls is much longer than
the list of supported ones, the table below mentions only the syscalls having some
degree of support. The missing syscalls have to be considered as *not implemented yet*.


 Syscall             | Support level
---------------------|-------------------------
 sys_exit            | full
 sys_fork            | full
 sys_read            | full
 sys_write           | full
 sys_open            | partial [1]
 sys_close           | full
 sys_waitpid         | compliant [2]
 sys_execve          | full
 sys_chdir           | full
 sys_getpid          | full
 sys_setuid16        | compliant [3]
 sys_getuid16        | compliant [3]
 sys_pause           | stub
 sys_access          | partial
 sys_brk             | full
 sys_setgid16        | compliant [3]
 sys_getgid16        | compliant [3]
 sys_seteuid16       | compliant [3]
 sys_setegid16       | compliant [3]
 sys_ioctl           | minimal
 sys_getppid         | full
 sys_gettimeofday    | full
 sys_munmap          | full
 sys_wait4           | partial
 sys_newuname        | full
 sys_llseek          | full
 sys_readv           | full
 sys_writev          | full
 sys_nanosleep       | full
 sys_prctl           | stub
 sys_getcwd          | full
 sys_mmap_pgoff      | partial
 sys_stat64          | full
 sys_lstat64         | partial
 sys_getuid          | compliant [3]
 sys_getgid          | compliant [3]
 sys_geteuid         | compliant [3]
 sys_getegid         | compliant [3]
 sys_setuid          | compliant [3]
 sys_setgid          | compliant [3]
 sys_getdents64      | full
 sys_fcntl64         | stub
 sys_gettid          | minimal
 sys_set_thread_area | full
 sys_exit_group      | stub
 sys_set_tid_address | stub
 sys_clock_gettime   | partial
 
 
Definitions:

 Support level | Meaning
---------------|---------------------------
 full          | Syscall fully supported
 stub          | The syscall does not return -ENOSYS, but it has actually a stub implementation, at the moment
 partial       | Syscall partially supported, work-in-progress
 minimal       | Minimal support for the syscall: only a few of its features are supported.
 compliant     | Syscall supported in a way compliant with a full implementation, but actually it has limitations due to the                  different design of Tilck. Example: see the note [3].
 
Notes:

1. open() work both for reading and writing files, but 'flags' and 'mode' are ignored at the moment.
   Also, it is possible only to write special files in /dev at the moment because the filesystem used
   by Tilck is only read-only, at the moment.
2. The cases pid < -1, pid == -1 and pid == 0 are treated in the same way because Tilck does not support
   multiple users/groups. See note [3].
3. Tilck does not support *by design* multiple users nor any form of authentication. Therefore, the following
   statement is always true: UID == GID == EUID == EGID == 0. The syscall setuid() is compliant because it
   succeeds when uid == 0.
        

FAQ (by vvaltchev)
---------------------


#### Why many commit messages are so short and incomplete?

It is well-known that all of the popular open source projects care about having good commit messages.
It is an investment that at some point pays off. I even wrote a [blog post](https://blogs.vmware.com/opensource/2017/12/28/open-source-proprietary-software-engineer/) about that.
The problem is that such investment actually starts paying off only when multiple people contribute to the project.
Even in the case of small teams (2 people) it not obvious that it is worth spending hours in re-ordering and editing all the commits of a pull request until its *story* is perfect, especially when the project is not mature enough: the commits in a pull request have to be just *good enough* in terms of commit message, scope of the change, relative order etc. The focus is on shape of the code *after* the patch series in the sense that limited hacks in the middle of a series are allowed. As a second contributor comes in, the commit messages will need necessarily to become more descriptive, in order to allow the collaboration to work. But, at this stage, going as fast as possible towards the first milestone makes sense. As the projects matures, I'll be spending more and more time on writing better commit messages.


#### Why Tilck does not have the feature/abstraction XYZ like other kernels do?

`Tilck` is **not** meant to be a full-featured production kernel. Please, refer to Linux for that.
The idea is implementing the simplest kernel able to run a class of Linux console applications.
After that, eventually, it can support more advanced stuff like USB mass storage devices,
but not necessarily along with all the powerful features that Linux offers.
The whole point of supporting something is because it is interesting for me (or other contributors)
to learn how it works and to write a minimalistic implementation to support it.


#### Why using FAT32?

Even if FAT32 is today the only filesystem supported by `Tilck`, in the next months it will
be used only as an initial read-only ramdisk. The main filesystem will be a custom ramfs, while
the FAT32 ramdisk will remain mounted (likely) under /boot. The #1 reason for using FAT32 was that
it is required for booting using UEFI. Therefore, it was convienent to store there also all the rest
of the files.




