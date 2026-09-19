# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import
#
# Whether the VMs run under KVM.
#
# "KVM is usable" means every link of the chain holds, not just one:
#
#    1. this is Linux and /dev/kvm exists: the kvm module is loaded,
#       which it is only when the CPU has virtualization support and
#       the firmware has not disabled it;
#    2. this process can open /dev/kvm read-write (group / udev rules);
#    3. the QEMU binary that will run the VM has the KVM accelerator
#       built in (`-accel help`): a distro or stack build may not, and
#       a qemu-system-<arch> for an arch other than the host's never
#       has it;
#    4. that binary actually comes up with -enable-kvm.
#
# Step 4 is the one that counts: it is the same binary, run the same
# way, with no machine and no devices, quit from the monitor as soon as
# it is up. Steps 1-3 exist to say *why* when it cannot.

import os
import re
import sys
import grp
import errno
import shutil
import subprocess

from .stdio import *
from .env import *

KVM_DEV = '/dev/kvm'
QEMU_PROBE_TIMEOUT = 10

kvm_installed = False
qemu_kvm_version = None

def is_kvm_installed():
   return kvm_installed

def get_qemu_kvm_version():

   if not kvm_installed:
      return ""

   if not qemu_kvm_version:
      return "<unknown>"

   return qemu_kvm_version

def set_qemu_kvm_version(version):

   global kvm_installed, qemu_kvm_version

   kvm_installed = True
   qemu_kvm_version = version

def kvm_dev_group():

   gid = os.stat(KVM_DEV).st_gid

   try:
      return grp.getgrgid(gid).gr_name
   except KeyError:
      return str(gid)

def kvm_dev_unusable_reason():
   """
   Why this process cannot use the kernel side of KVM, or None.
   """

   if not sys.platform.startswith('linux'):
      return "KVM is Linux-only (host: {})".format(sys.platform)

   if not os.path.exists(KVM_DEV):
      return (
         "{} does not exist: the kvm module is not loaded, or the CPU has "
         "no virtualization support, or it is disabled in the firmware"
         .format(KVM_DEV)
      )

   try:
      os.close(os.open(KVM_DEV, os.O_RDWR))
   except OSError as e:

      hint = ""

      if e.errno in (errno.EACCES, errno.EPERM):
         hint = " (add the user to the '{}' group?)".format(kvm_dev_group())

      return "cannot open {}: {}{}".format(KVM_DEV, e.strerror, hint)

   return None

def qemu_accelerators(qemu_bin):
   """
   The accelerators built into `qemu_bin`, as `-accel help` lists them:
   a header line, then one name per line.
   """

   out = subprocess.check_output(
      [qemu_bin, '-accel', 'help'],
      stderr = subprocess.STDOUT,
      timeout = QEMU_PROBE_TIMEOUT,
   ).decode('utf-8', 'replace')

   names = [l.strip() for l in out.splitlines()]
   return [n for n in names if re.fullmatch(r'\w+', n)]

def qemu_kvm_unusable_reason(qemu_bin):
   """
   Why `qemu_bin` (looked up in PATH, as the runners do) cannot run a VM
   under KVM, or None.
   """

   if not shutil.which(qemu_bin):
      return "{} not found in PATH".format(qemu_bin)

   try:
      accels = qemu_accelerators(qemu_bin)
   except (OSError, subprocess.SubprocessError) as e:
      return "'{} -accel help' failed: {}".format(qemu_bin, e)

   if 'kvm' not in accels:
      return "{} was built without KVM support (accelerators: {})".format(
         qemu_bin, ", ".join(accels) or "none listed"
      )

   cmd = [
      qemu_bin,
      '-enable-kvm',
      '-machine', 'none',
      '-display', 'none',
      '-nodefaults',
      '-monitor', 'stdio',
   ]

   try:
      p = subprocess.run(
         cmd,
         input = b'quit\n',
         stdout = subprocess.DEVNULL,
         stderr = subprocess.PIPE,
         timeout = QEMU_PROBE_TIMEOUT,
      )
   except (OSError, subprocess.SubprocessError) as e:
      return "'{}' failed: {}".format(" ".join(cmd), e)

   if p.returncode != 0:

      err = p.stderr.decode('utf-8', 'replace').strip().splitlines()

      return "{} cannot start in KVM mode: {}".format(
         qemu_bin, err[0] if err else "exit code {}".format(p.returncode)
      )

   return None

def qemu_version(qemu_bin):

   out = subprocess.check_output(
      [qemu_bin, '--version'], timeout = QEMU_PROBE_TIMEOUT
   ).decode('utf-8', 'replace')

   m = re.search(r'QEMU.*version +((?:[0-9]+[.])+[0-9]+)', out)
   return m.group(1) if m else None

def detect_kvm(qemu_bin):
   """
   Decide whether the VMs run under KVM, with `qemu_bin` the binary the
   runners start (a name looked up in PATH). Anything short of the whole
   chain holding means TCG, and the reason is printed.
   """

   global kvm_installed, qemu_kvm_version

   if IN_ANY_CI:
      raw_print("IN_ANY_CI=1, assuming KVM is not usable")
      return

   if qemu_kvm_version:
      raw_print("Assumed QEMU (KVM) version: {}".format(qemu_kvm_version))
      return

   reason = kvm_dev_unusable_reason() or qemu_kvm_unusable_reason(qemu_bin)

   if reason:
      raw_print(
         "\n"
         "*** WARNING: KVM is not usable: {} ***\n"
         "Running the tests without hardware virtualization is slow and "
         "inefficient.\n".format(reason)
      )
      return

   kvm_installed = True

   try:
      qemu_kvm_version = qemu_version(qemu_bin)
   except (OSError, subprocess.SubprocessError):
      qemu_kvm_version = None

   raw_print(
      "Detected KVM, usable by {} version {}"
      .format(qemu_bin, get_qemu_kvm_version())
   )

def print_timeout_kill_vm_msg(timeout):
   msg_print(
      "The VM is alive after the timeout "
      "of {} seconds. KILLING IT.".format(timeout)
   )
