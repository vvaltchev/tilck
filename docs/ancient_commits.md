How to build and test the ancient versions of Tilck
-------------------------------------------------------------------------------------

**Key-commits:**
   * z-ancient-01 "makefile works well. Removing make_iso.sh"
   * z-ancient-02 "fixing link2.ld and makefile"

**Key-commits** (Cygwin only):
   * z-ancient-02-cygwin-01 "implementing return from syscalls"
   * z-ancient-02-cygwin-02 "the usermode printf works"
   * z-ancient-02-cygwin-03 "raising the term buf to 1024 lines and the scroll to 5 lines"

**Notes:**
   The earliest versions of Tilck (called exOS at the time), have been
   developed under Windows using Cygwin and Cygwin's GCC 5.3, not MinGW.
   Therefore, compiling them is a bit tricky: the simplest setup requires
   an old 32-bit system (better if Ubuntu or Debian) and fair amount of patience.
   Also, keep in mind that many untagged commits might not work at all, especially
   with the Linux setup: they worked only using the *exact* configuration
   used at the time (see the Cygwin instructions below). Expect triple-faults
   and any kind of weirdness, especially with the Linux setup.

**Linux instructions** (simpler, but less reliable):

1. Pick up an old 32-bit Linux distro like Ubuntu 14.04 LTS

2. Install the following packages:
   git, gcc, nasm, GNU make, gcc-mingw-w64-i686, qemu-system-i386

3. Enter in the Tilck main directory and run (using Bash):
   ```
   w=/usr/bin/i686-w64-mingw32 && mkdir bin &&                     \
   $(cd $_ && for x in gcc ld objcopy; do ln -s $w-$x $x; done) && \
   export PATH="`pwd`/bin:$PATH"
   ```
4. Compile with: `make`

5. Run: `qemu-system-i386 -fda os2.img`

**Cygwin instructions** (more complicated, but 99% close to the original setup):

1. Pick up a Windows system. At the time Windows 7 64-bit was used,
   but an up-to-date Windows 10 works too.

2. Install a 32-bit version of Cygwin from the Cygwin Time Machine:

   http://ctm.crouchingtigerhiddenfruitbat.org/pub/cygwin/setup/setup.html

   Even the latest 32-bit version (2.918) will work. On Windows 10. The original
   setup used Windows 7. It's not clear if that might make a difference.

3. Run from a Windows Command Prompt as Administrator: `setup-x86.exe -X`.
   That is necessary in order to use the Cygwin Time Machine mirrors.

4. When asked to choose the mirror, use:
   http://ctm.crouchingtigerhiddenfruitbat.org/pub/cygwin/circa/2016/03/02/124013

   Or, in general, one from March 2016. The full list is here:
   http://ctm.crouchingtigerhiddenfruitbat.org/pub/cygwin/circa/index.html

5. Install the cygwin gcc 5.3.0 package (not mingw!), nasm, GNU make and git.

6. Run: `git config --global core.autocrlf false`

7. Clone Tilck's repo with: `git clone https://github.com/vvaltchev/tilck`

8. Checkout an ancient commit, before 4e8c99d6c8cc3dae5ef6faf52968c389939e5b74,
   like z-ancient-01.

9. Build the project with: `./make_iso.sh` or `make`, depending on the commit.

10. Pick-up and install QEMU for Windows from here:
    https://www.qemu.org/download/#windows
    The following version has been tested to work on Windows 10:
    https://qemu.weilnetz.de/w64/qemu-w64-setup-20220419.exe

11. Run the Win32 QEMU from Cygwin to test the kernel with:
    ```
    "/cygdrive/c/Program Files/qemu/qemu-system-i386.exe" -fda os2.img
    ```

12. [Optional] Install Bochs 2.7, the simulator originally used in the early
    commits. Run bochsdbg.exe and create a configuration adding os2.img as a
    3.5" 1.44 MB floppy image. Run the simulation. Press "c" to continue
    when we hit the Bochs "magic debug breaks". Note: don't run the regular
    non-dbg version of Bochs, as it won't work because of the debug breaks.

13. Enjoy having spent an insane amount of time just to build
    a few ancient commits of this amazing project!

-------------------------------------------------------------------------------------

**Key-commits:**
   * z-ancient-03 "making the kernel buildable only under a 32-bit linux"

**Notes:**
   Commits in this group can be natively built on 32-bit Linux systems, with GCC 4.8.
   The Cygwin build has been abandoned because clearly was not good in the long term.

**Instructions:**

1. Pick up an old *32-bit* Linux distro like Ubuntu 14.04 LTS

2. Install the following packages:
   git, gcc, nasm, GNU make, qemu-system-i386

2. Compile with: `make`

3. Run: `qemu-system-i386 -fda os2.img`

-------------------------------------------------------------------------------------

**Key-commits:**
   * f3e41afe14aa "making the kernel buildable under linux64"
   * z-ancient-04 "moving qtcreator files in a dedicated dir and other minor stuff"
   * z-ancient-05 "adding int64 support on the 32-bit kernel and making RDTSC to work correctly"

**Notes:**
   Commits in this group can be natively built on 64-bit Linux systems, with GCC 4.8.
   Keyboard echo and basic syscalls from userspace work. The first version of kmalloc()
   appeared here. Unit tests have been just introduced in the more recent commits in this group.

**Instructions:**

1. Pick up an old *64-bit* Linux distro like Ubuntu 14.04 LTS

2. Install the following packages:
   git, gcc, nasm, GNU make, qemu-system-i386

3. Compile the kernel with: `make`

4. [optional] Compile the unit tests with: `make tests`

5. Run: `./run_qemu_linux` or `./run_qemu`

6. [optional] Run the unit tests with: `./build/unittests`

-------------------------------------------------------------------------------------

**Key-commits:**
   * z-ancient-06 "making some scrips executable"

**Notes:**
   Commits in this group are built with CMake for the first time.
   A simple `build_toolchain.sh` script has been just introduced.

**Instructions:**

   1. Pick up an old *64-bit* Ubuntu like 16.04 LTS, but more recent than 14.04 LTS.
      The GCC 5.4.0 compiler is supported here.

   2. Install git, cmake, qemu-system-x86.

   3. Run the follow script to install automatically the required packages (Ubuntu-only)
      and other stuff in the toolchain: `./build_toolchain.sh`

   4. Configure the CMake build with: `mkdir build && cd build && cmake ..`

   5. Compile with (from the build/ directory): `make`

   6. Test the OS with: `./run_qemu`

-------------------------------------------------------------------------------------

**Key-commits:**
   * z-ancient-07 "Add errno.h in the kernel"

**Notes:**
   Commits in this group start to have a more powerful build_toolchain script
   and, for the first time, we start to download a toolchain from bootlin.
   The nasm compiler is not required on the system anymore and it's downloaded
   in the toolchain.

**Instructions:**

   1. Pick up an old *64-bit* Ubuntu like 16.04 LTS, but more recent than 14.04 LTS.
   2. Install: git, cmake, qemu-system-x86.
   3. Run: `./scripts/build_toolchain`
   4. Configure the CMake build with: `mkdir build && cd build && cmake ..`
   5. Compile with: `make` (from the build/ directory)
   6. Test the OS with: `./run_qemu`

-------------------------------------------------------------------------------------

**Key-commits:**
   * z-ancient-08 "Update README"

**Notes:**
   Commits in this group start to have a better build_toolchain script
   and better CMake code. The unit tests now use the Googletest framework
   and the whole tooling around that is stable. This is the last commit
   before the project started to be developed full-time.

**Instructions:**

   1. Pick up an old *64-bit* Ubuntu like 16.04 LTS, but more recent than 14.04 LTS.
   2. Install: git, cmake, qemu-system-x86.
   3. Run: `./scripts/build_toolchain`
   4. Configure the CMake build and build with just: `make`
   5. Test the OS with: `./run_qemu`
   6. [Optional] Build unit tests with: `make gtests`
   7. [Optional] Run the unit tests with: `./build/gtests`

-------------------------------------------------------------------------------------

**Key-commits:**
   * z-ancient-09 "[paging] Rename 'pageTableAddr' to 'ptaddr'"
   * z-ancient-10 "[shell] Implement the cd command"
   * z-ancient-11 "[self_tests] Fix fault_resumable_perf"
   * z-ancient-12 "[shell] Use asm to do the "int 0x80" syscall in cmd_syscall_perf"

**Notes:**
   Commits in this group work for the first time on Ubuntu 18.04 LTS.
   At this point, started a bit of serious development for filesystem syscalls
   like read(), write() etc. Process management, scheduling and memory management
   have a fair amount of functionalities. The framebuffer console and the
   interactive EFI bootloader have been added here.

   Ubuntu 18.04 is required because the run_qemu scripts pass deprecated
   options to qemu-system-i386, which have been removed in the newer releases
   of QEMU.

**Instructions:**

   1. Pick up *64-bit* Ubuntu 18.04 LTS.
   2. Install: git.
   3. Run: `./scripts/build_toolchain`
   4. Configure the CMake build and build with just: `make`
   5. Test the OS with: `./build/run_qemu` (or `./build/run_qemu`)
   6. [Optional] Test the UEFI boot with: `./build/run_efi_qemu32` [requires extra toolchain steps]
   7. [Optional] Build unit tests with: `make gtests`
   8. [Optional] Run the unit tests with: `./build/gtests`

-------------------------------------------------------------------------------------

**Key-commits:**
   * z-ancient-13 "Update README.md"

**Notes:**
   The commits in this group finally have a minimal devshell with `ls` working.
   The musl library is used by default. The shell forwards the unknown commands
   to busybox. Also, project's name changes from "experimentOS" to "Tilck".

**Instructions:**
   Exactly as above. Ubuntu 18.04 LTS is still required because of the QEMU scripts.

-------------------------------------------------------------------------------------

**Key-commits:**
   * z-ancient-14 "[term] Fix a bug related with \n + improve debug_per_task_cb()"
   * z-ancient-15 "Merge pull request #57 from vvaltchev/term"

**Notes:**
   This is the last group of "ancient" commits before the stable-* series
   starts. Here the devshell and the console get improved. Multiple TTYs
   are supported.

**Instructions:**
   Exactly as above. Ubuntu 18.04 LTS is still required because of the QEMU scripts.


-------------------------------------------------------------------------------------

Starting from the commit tagged as stable-001, it's possible to build and run Tilck
on modern Linux distributions like Ubuntu 20.04 LTS. The devshell has the `runall`
command and Busybox's version of `vi` works. Unfortunately, some scripts like
`run_all_tests` still require Python 2.7.x. That has been fixed between stable-004
and stable-005.
