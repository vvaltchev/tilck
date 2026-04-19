# SPDX-License-Identifier: BSD-2-Clause
#
# Per-module options for `pci`. Included automatically by the MOD_*
# loop in the root CMakeLists.txt after MOD_pci is declared. mconf
# hides the whole sub-menu when MOD_pci is off.

tilck_option(KRN_PCI_VENDORS_LIST
   TYPE     BOOL
   CATEGORY "Modules/pci"
   DEFAULT  OFF
   DEPENDS  MOD_pci
   HELP     "Compile-in the full PCI vendors list"
)
