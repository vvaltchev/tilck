# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import
#
# A QMP client for one QEMU process.
#
# The runner listens on a Unix socket and QEMU connects to it, with
# `-qmp unix:<path>` and nothing else: the original QMP command line,
# with no server/wait options, so it reads the same on every QEMU since
# 0.13. The runner is listening before QEMU is spawned, so there is no
# "has QEMU created the socket yet" loop either: accept() returns
# whenever QEMU gets there.
#
# The socket lives in its own temp dir, not in the build tree: a Unix
# socket path is limited to ~104 bytes and a build directory can be
# anywhere.
#
# Replies and events are JSON objects on a byte stream. They are cut
# with raw_decode over an accumulating buffer, not by line: QEMU
# happens to end each object with a newline, the protocol does not
# promise it.

import os
import json
import time
import codecs
import socket
import shutil
import tempfile

from .stdio import *
from .env import *

class QmpError(Exception):
   pass

class QmpCommandError(QmpError):
   """
   QEMU answered a command with an error reply.
   """
   def __init__(self, cmd, error):
      self.error_class = error.get("class", "?")
      self.desc = error.get("desc", "")
      super().__init__(
         "QMP '{}' failed: {}: {}".format(cmd, self.error_class, self.desc)
      )

class QemuGone(QmpError):
   """
   The connection is gone: QEMU exited, or was killed.
   """

class QmpTimeout(QmpError):
   """
   QEMU did not connect, or did not reply, within the timeout.
   """

class QmpClient:

   def __init__(self, timeout):

      self.timeout = timeout
      self.tmp_dir = tempfile.mkdtemp(prefix = "tilck-qmp-")
      self.path = os.path.join(self.tmp_dir, "qmp.sock")
      self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
      self.conn = None
      self.buf = ""
      self.utf8 = codecs.getincrementaldecoder("utf-8")()
      self.decoder = json.JSONDecoder()
      self.events = []

      self.listener.bind(self.path)
      self.listener.listen(1)

   @property
   def connected(self):
      return self.conn is not None

   @property
   def qemu_arg(self):
      """
      The value for QEMU's -qmp option.
      """
      return "unix:" + self.path

   def accept(self, alive = lambda: True):
      """
      Wait for QEMU to connect, read its greeting and negotiate the
      capabilities. Returns the greeting's version dict. `alive` is
      polled while waiting: a QEMU that exits before connecting (a bad
      command line) is reported at once, not after the timeout.
      """

      deadline = time.monotonic() + self.timeout
      self.listener.settimeout(0.5)

      while True:

         try:
            self.conn, unused = self.listener.accept()
            break
         except socket.timeout:
            pass

         if not alive():
            raise QemuGone("QEMU exited before connecting to QMP")

         if time.monotonic() >= deadline:
            raise QmpTimeout(
               "QEMU did not connect to {} within {}s"
               .format(self.path, self.timeout)
            )

      self.conn.settimeout(self.timeout)
      greeting = self.read_message()

      if "QMP" not in greeting:
         raise QmpError("Not a QMP greeting: {}".format(greeting))

      self.cmd("qmp_capabilities")
      return greeting["QMP"]["version"]

   def read_message(self):
      """
      The next JSON object from QEMU, a reply or an event.
      """

      while True:

         text = self.buf.lstrip()

         if text:

            try:
               obj, end = self.decoder.raw_decode(text)
            except ValueError:
               pass # incomplete: read more
            else:
               self.buf = text[end:]
               return obj

         try:
            chunk = self.conn.recv(65536)
         except socket.timeout:
            raise QmpTimeout(
               "No reply from QEMU within {}s".format(self.timeout)
            ) from None
         except OSError as e:
            raise QemuGone("QMP connection lost: {}".format(e)) from None

         if not chunk:
            raise QemuGone("QEMU closed the QMP connection")

         self.buf = text + self.utf8.decode(chunk)

   def cmd(self, name, **args):
      """
      Execute one command and return its result. Events that arrive
      in the meantime are recorded and skipped.
      """

      msg = {"execute": name}

      if args:
         msg["arguments"] = args

      if not self.connected:
         raise QmpError("Not connected to QEMU")

      try:
         self.conn.sendall((json.dumps(msg) + "\n").encode("utf-8"))
      except OSError as e:
         raise QemuGone("QMP connection lost: {}".format(e)) from None

      while True:

         m = self.read_message()

         if "event" in m:
            self.on_event(m)
            continue

         if "error" in m:
            raise QmpCommandError(name, m["error"])

         return m["return"]

   def on_event(self, m):

      self.events.append(m)

      if VERBOSE:
         msg_print("QMP event: {}".format(m["event"]))

   def close(self):

      for s in (self.conn, self.listener):
         if s:
            s.close()

      self.conn = None
      shutil.rmtree(self.tmp_dir, ignore_errors = True)
