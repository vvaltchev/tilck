# experimentOS (exOS)

A Linux-compatible x86 kernel written for educational purposes.

From the technical point of view, the goal of this project is writing a kernel being able to run *natively* x86 Linux console applications (like shells, text editors and compilers), without having to rebuild any part of them. The kernel will support only a subset of today's Linux syscalls but that subset will be determinied by the minimum necessary to run a given set of applications. Briefly, making typical applications like `bash`, `ls`, `cat`, `sed`, `vi`, `gcc` to work correctly, is a *must*. At the moment, it is not part of project's main to goal the kernel to actually have disk drivers, nor graphic ones: everything will be loaded at boot time and only a ram disk will be available. At most, if possible, a simple network driver will be implemented or an actual driver will be ported from an open-source kernel (Linux or FreeBSD).

Once the main goal is achieved, the simple kernel could be actually used for any kind of kernel-development *experiments* with the advantage that changes will be *orders of magnitude simpler* to implement in **exOS** compared to doing that in the Linux kernel. Also, since from it very first commit this project used git, students learning operating systems may enjoy the experience of being able to see how a minimalistic kernel can be written, from the first lines of its legacy bootloader to the implementation of its most complex syscalls.












