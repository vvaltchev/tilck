# SPDX-License-Identifier: BSD-2-Clause

import sys

# Runtime config vars
runnerName = "<UNKNOWN>"

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
