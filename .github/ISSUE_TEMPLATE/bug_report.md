---
name: Bug report
about: Create a report to help us improve
title: ''
labels: bug
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.

**Host configuration**
- Linux distro and version
- Compiler (name and version)
- Host CPU architecture

**Type of issue (select one)**
- Toolchain build problem
- Tilck build problem
- Tilck boot problem
- Tilck test failure
- Incorrect behavior
- Panic
- Other

**Reproduction details**
- Is the issue reproducible in a deterministic way?
- Is the issue reproducible both on real hardware and QEMU or just on real HW?
- If applicable, describe the hardware machine used to run Tilck.

**Toolchain configuration**
- How was the `build_toolchain` script run? (e.g. environment variables)

**Tilck build configuration**
- How did `cmake` was run? Did you use the `run_cmake` wrapper?
  Please, attach the full build log of a *clean* build.
- Tilck commit: please indicate the exact commit used to reproduce the problem
- Special configurations? (e.g. coverage)

**Expected behavior**
A clear and concise description of what you expected to happen.

**Screenshots**
If applicable, add screenshots to help explain your problem.

**Additional context**
Add any other context about the problem here.
