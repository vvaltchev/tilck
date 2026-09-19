# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

# The PC platform. There is no board data here: the firmware (BIOS or
# UEFI) describes the machine, and the bootloaders under boot/ are
# Tilck's own. The board exists all the same, because every arch has
# a board -- it is a coordinate of every installed package -- and a
# board named on the command line must be one of the arch's: this
# file is what BOARD=pc resolves to, and BOARD=anything-else does not.
