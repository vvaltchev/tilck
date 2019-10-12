# SPDX-License-Identifier: BSD-2-Clause
import sys

def raw_print(msg):
   sys.stdout.buffer.write(msg.encode('utf-8'))
   sys.stdout.buffer.write('\n'.encode('utf-8'))

def msg_print(msg):
   raw_print("[system test runner] {}".format(msg))
