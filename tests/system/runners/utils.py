# SPDX-License-Identifier: BSD-2-Clause
import re
import os
import sys
import subprocess
from enum import Enum

# Constants

class Fail(Enum):
   success                 = 0
   invalid_args            = 1
   reboot                  = 2
   timeout                 = 3
   panic                   = 4
   shell_no_zero_exit      = 5
   gcov_error              = 6
   shell_unknown_exit_code = 7

def getFailByCode(err_code):

   for f in Fail:
      if f.value == err_code:
         return f

   return None

test_types = ['selftest', 'shellcmd']

# Constants coming from CMake (this file gets pre-processed by CMake)
KERNEL_FILE = r'@KERNEL_FILE@'
BUILD_DIR = r'@CMAKE_BINARY_DIR@'

# Environment flags
in_travis = os.environ.get('TRAVIS', False)
in_circleci = os.environ.get('CIRCLECI', False)
in_azure = os.environ.get('AZURE_HTTP_USER_AGENT', False)
dump_coverage = os.environ.get('DUMP_COV', False)
report_coverage = os.environ.get('REPORT_COV', False)
verbose = os.environ.get('VERBOSE', False)
in_any_ci = in_travis or in_circleci or in_azure

# Runtime config vars
runnerName = "system test runner"
kvm_installed = False
qemu_kvm_version = None

def direct_print(data):
   sys.stdout.buffer.write(data)
   sys.stdout.buffer.flush()

def raw_print(msg):
   sys.stdout.buffer.write(msg.encode('utf-8'))
   sys.stdout.buffer.write('\n'.encode('utf-8'))
   sys.stdout.buffer.flush()

def raw_stdout_write(msg):
   sys.stdout.buffer.write(msg.encode('utf-8'))
   sys.stdout.buffer.flush()

def msg_print(msg):
   raw_print("[{}] {}".format(runnerName, msg))

def set_runner_name(name):
   global runnerName
   runnerName = name

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
      if not in_any_ci:
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
