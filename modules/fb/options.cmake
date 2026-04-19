# SPDX-License-Identifier: BSD-2-Clause
#
# Per-module options for `fb`. Included automatically by the MOD_*
# loop in the root CMakeLists.txt after MOD_fb is declared. These
# options share the "Modules" category with MOD_fb itself, so in
# mconf they show up directly after the MOD_fb toggle and are
# hidden (via DEPENDS MOD_fb) when the module is off.

tilck_option(KRN_FBCON_BIGFONT_THR
   TYPE     UINT
   CATEGORY "Modules"
   DEFAULT  160
   DEPENDS  MOD_fb
   HELP     "Cols threshold to switch to 16x32 fb_console font"
            "When the screen has more columns than this threshold,"
            "fb_console switches from the default 8x16 font to a"
            "16x32 font."
)

tilck_option(KRN_FB_CONSOLE_BANNER
   TYPE     BOOL
   CATEGORY "Modules"
   DEFAULT  ON
   DEPENDS  MOD_fb
   HELP     "Show a banner at the top of fb_console"
)

tilck_option(KRN_FB_CONSOLE_CURSOR_BLINK
   TYPE     BOOL
   CATEGORY "Modules"
   DEFAULT  ON
   DEPENDS  MOD_fb
   HELP     "Enable cursor blinking in fb_console"
)

tilck_option(KRN_FB_CONSOLE_USE_ALT_FONTS
   TYPE     BOOL
   CATEGORY "Modules"
   DEFAULT  OFF
   DEPENDS  MOD_fb
   HELP     "Use alternate fonts from other/alt_fonts/"
)

tilck_option(KRN_FB_CONSOLE_FAILSAFE_OPT
   TYPE     BOOL
   CATEGORY "Modules"
   DEFAULT  OFF
   DEPENDS  MOD_fb
   HELP     "Optimize fb_console failsafe mode for old machines"
)
