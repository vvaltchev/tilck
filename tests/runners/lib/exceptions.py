# SPDX-License-Identifier: BSD-2-Clause

# Some constants
STABLE_SCREENSHOT_DELAY   = 0.25
STABLE_SCREENSHOT_TIMEOUT = 3.00

class InvalidSystemConfig(Exception):
   def __init__(self, msg):
      super(InvalidSystemConfig, self).__init__(msg)

class NoTilckHelloMessage(Exception):
   def __init__(self, screen_text = None):
      super(NoTilckHelloMessage, self).__init__("NoTilckHelloMessage")
      self.screen_text = screen_text

class KernelPanicFailure(Exception):
   def __init__(self, screen_text = None):
      super(KernelPanicFailure, self).__init__("KernelPanicFailure")
      self.screen_text = screen_text

class StableScreenshotFailure(Exception):
   def __init__(self):
      msg = "Unable to take a stable screenshot in {}s"
      msg = msg.format(STABLE_SCREENSHOT_TIMEOUT)
      super(StableScreenshotFailure, self).__init__(msg)

class ConvertFailure(Exception):
   def __init__(self, msg):
      super(ConvertFailure, self).__init__(msg)

class Pnm2TextFailure(Exception):
   def __init__(self, ret):
      super(Pnm2TextFailure, self).__init__("Return code: {}".format(ret))

class IntTestScreenTextCheckFailure(Exception):
   def __init__(self, screen_text):
      super(IntTestScreenTextCheckFailure, self)    \
         .__init__("IntTestScreenTextCheckFailure")
      self.screen_text = screen_text
