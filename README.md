# experimentOS (exOS)

A Linux-compatible x86 kernel written for educational purposes.

From the technical point of view, the goal of this project is writing a kernel being able to run *natively* x86 Linux console applications (like shells, text editors and compilers), without having to rebuild any part of them. The kernel will support clearly only a subset of the syscalls supported by Linux today but that subset will be determinied by the minimum necessary to run a given set of applications. Briefly, making typical applications like `bash`, `ls`, `cat`, `sed`, `vi`, `gcc` to work correctly, is mandatory for claiming victory.

Once the main goal is achieved, the simple kernel could be actually used for any kind of kernel-development (academic or not) *experiments* with the advantage that changes will be *orders of magnitude simpler* to implement in **exOS** compared to doing that in the Linux kernel.








