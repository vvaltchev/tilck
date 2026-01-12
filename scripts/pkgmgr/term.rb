# SPDX-License-Identifier: BSD-2-Clause

module Term

  RED = "\e[0;31m"
  GREEN = "\e[0;32m"
  YELLOW = "\e[1;33m"
  BLUE = "\e[1;34m"
  MAGENTA = "\e[1;35m"
  WHITE = "\e[1;37m"
  RESET = "\e[0m"

  module_function
  def makeWhite(s) = "#{WHITE}#{s}#{RESET}"
  def makeRed(s) = "#{RED}#{s}#{RESET}"
  def makeGreen(s) = "#{GREEN}#{s}#{RESET}"
  def makeYellow(s) = "#{YELLOW}#{s}#{RESET}"
  def makeBlue(s) = "#{BLUE}#{s}#{RESET}"
  def makeMagenta(s) = "#{MAGENTA}#{s}#{RESET}"

end
