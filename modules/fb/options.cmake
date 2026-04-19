# SPDX-License-Identifier: BSD-2-Clause
#
# Per-module options for `fb`. Included automatically by the MOD_*
# loop in the root CMakeLists.txt after MOD_fb is declared. mconf
# hides the whole sub-menu when MOD_fb is off (every option here
# DEPENDS on MOD_fb).

tilck_option(KRN_FBCON_BIGFONT_THR
   TYPE     UINT
   CATEGORY "Modules/fb"
   DEFAULT  160
   DEPENDS  MOD_fb
   HELP     "Cols threshold to switch to 16x32 fb_console font"
            "When the screen has more columns than this threshold,"
            "fb_console switches from the default 8x16 font to a"
            "16x32 font."
)

tilck_option(KRN_FB_CONSOLE_BANNER
   TYPE     BOOL
   CATEGORY "Modules/fb"
   DEFAULT  ON
   DEPENDS  MOD_fb
   HELP     "Show a banner at the top of fb_console"
)

tilck_option(KRN_FB_CONSOLE_CURSOR_BLINK
   TYPE     BOOL
   CATEGORY "Modules/fb"
   DEFAULT  ON
   DEPENDS  MOD_fb
   HELP     "Enable cursor blinking in fb_console"
)

tilck_option(KRN_FB_CONSOLE_USE_ALT_FONTS
   TYPE     BOOL
   CATEGORY "Modules/fb"
   DEFAULT  OFF
   DEPENDS  MOD_fb
   HELP     "Use alternate fonts from other/alt_fonts/"
)

tilck_option(KRN_FB_CONSOLE_FAILSAFE_OPT
   TYPE     BOOL
   CATEGORY "Modules/fb"
   DEFAULT  OFF
   DEPENDS  MOD_fb
   HELP     "Optimize fb_console failsafe mode for old machines"
)
