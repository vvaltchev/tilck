# SPDX-License-Identifier: BSD-2-Clause

module Term

  RED = "\e[0;31m"
  GREEN = "\e[0;32m"
  YELLOW = "\e[1;33m"
  WHITE = "\e[1;37m"
  RESET = "\e[0m"

  module_function
  def makeRed(s) = "#{RED}#{s}#{RESET}"
  def makeGreen(s) = "#{GREEN}#{s}#{RESET}"
  def makeYellow(s) = "#{YELLOW}#{s}#{RESET}"
  def makeWhite(s) = "#{WHITE}#{s}#{RESET}"

end
