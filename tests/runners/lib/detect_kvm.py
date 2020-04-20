# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import re
import subprocess
import traceback

from .stdio import *
from .env import *

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

def detect_kvm():

   global kvm_installed, qemu_kvm_version

   try:
      r = subprocess.check_output(['kvm', '--version']).decode('utf-8')
      kvm_installed = True

      m = re.search('QEMU.*version +((?:[0-9]+[.])+[0-9]+)', r)

      if m:
         qemu_kvm_version = m.groups(0)[0]
         raw_print("Detected qemu-kvm, version: {}".format(qemu_kvm_version))
      else:
         raw_print("Detected qemu-kvm, but UNKNOWN version. Version string:")
         raw_print(r)

   except:

      if not IN_ANY_CI:
         raw_print(
            "\n"
            "*** WARNING: qemu-kvm not found on the system ***\n"
            "Running the tests without hardware virtualization is slow and "
            "inefficient.\n"
            "Install qemu-kvm on your system for a better performance."
            "\n"
         )
      pass


def print_timeout_kill_vm_msg(timeout):
   msg_print(
      "The VM is alive after the timeout "
      "of {} seconds. KILLING IT.".format(timeout)
   )
