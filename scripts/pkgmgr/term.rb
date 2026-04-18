# SPDX-License-Identifier: BSD-2-Clause

module Term

  # Basic ANSI colors (used by package status display)
  RED = "\e[0;31m"
  GREEN = "\e[0;32m"
  YELLOW = "\e[1;33m"
  BLUE = "\e[1;34m"
  MAGENTA = "\e[1;35m"
  WHITE = "\e[1;37m"
  RESET = "\e[0m"

  # xterm-256 colors (used by test runner and system tests)
  GREEN256 = "\e[38;5;40m"
  RED256   = "\e[38;5;196m"
  YELLOW256 = "\e[38;5;220m"
  CYAN256  = "\e[38;5;75m"
  GRAY256  = "\e[38;5;245m"

  # Text attributes
  BOLD = "\e[1m"
  DIM  = "\e[2m"

  # Reusable decorations
  HLINE = "#{DIM}#{"─" * 72}#{RESET}"

  module_function
  def makeWhite(s) = "#{WHITE}#{s}#{RESET}"
  def makeRed(s) = "#{RED}#{s}#{RESET}"
  def makeGreen(s) = "#{GREEN}#{s}#{RESET}"
  def makeYellow(s) = "#{YELLOW}#{s}#{RESET}"
  def makeBlue(s) = "#{BLUE}#{s}#{RESET}"
  def makeMagenta(s) = "#{MAGENTA}#{s}#{RESET}"

end
