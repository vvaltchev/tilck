# SPDX-License-Identifier: BSD-2-Clause
#
# Per-module options for `pci`. Included automatically by the MOD_*
# loop in the root CMakeLists.txt after MOD_pci is declared. Shares
# the "Modules" category with MOD_pci itself, so in mconf it shows
# up directly after the MOD_pci toggle and is hidden (via DEPENDS
# MOD_pci) when the module is off.

tilck_option(KRN_PCI_VENDORS_LIST
   TYPE     BOOL
   CATEGORY "Modules"
   DEFAULT  OFF
   DEPENDS  MOD_pci
   HELP     "Compile-in the full PCI vendors list"
)
