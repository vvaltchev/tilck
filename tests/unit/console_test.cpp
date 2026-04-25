/* SPDX-License-Identifier: BSD-2-Clause */
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <vector>
#include <random>
#include <memory>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "fake_funcs_utils.h"
#include "kernel_init_funcs.h"
#include "mocking.h"

extern "C" {
   #include <tilck/kernel/term.h>
   #include <tilck/kernel/tty_struct.h>
   #include <tilck/kernel/test/tty_test.h>
   #include <tilck/kernel/ringbuf.h>
   #include <tilck/kernel/sched.h>
   #include <tilck/common/color_defs.h>

   /*
    * Declared in the internal header kernel/tty/tty_int.h — we can't
    * include that from tests (it's private to the tty subsystem), but the
    * symbol is exported from kernel/tty/tty_input.c and gets linked into
    * the test binary via libkernel_test_patched.a.
    */
   void tty_input_init(struct tty *t);
}

using namespace std;
using namespace testing;

#define TEST_TERM_ROWS  5
#define TEST_TERM_COLS 20

static u16 test_video_framebuffer[TEST_TERM_ROWS][TEST_TERM_COLS];
static u16 cursor_row;
static u16 cursor_col;
static bool cursor_enabled = true;

static void console_test_dump_char(int row, int col, bool safe)
{
   if (cursor_enabled && row == cursor_row && col == cursor_col) {

      printf("$");

   } else {

      char c = vgaentry_get_char(test_video_framebuffer[row][col]);

      if (safe) {
         if (!isprint(c))
            c = '.';
      }

      printf("%c", c);
   }
}

static void console_test_dump_screen(bool safe)
{
   printf("+");
   for (int j = 0; j < TEST_TERM_COLS; j++) {
      printf("-");
   }
   printf("+");
   printf("\n");

   for (int i = 0; i < TEST_TERM_ROWS; i++) {
      printf("|");
      for (int j = 0; j < TEST_TERM_COLS; j++) {
         console_test_dump_char(i, j, safe);
      }
      printf("|\n");
   }

   printf("+");
   for (int j = 0; j < TEST_TERM_COLS; j++) {
      printf("-");
   }
   printf("+");
   printf("\n");
}

static void check_screen_vs_expected(const char *exp_screen)
{
   const char *p = exp_screen;
   bool cursor_checked = false;

   /* Test check that expected screen border is correct */
   ASSERT_EQ(*p++, '\n');
   while (*p == ' ') p++;
   ASSERT_EQ(*p++, '+');
   for (int i = 0; i < TEST_TERM_COLS; i++) {
      ASSERT_EQ(*p++, '-');
   }
   ASSERT_EQ(*p++, '+');
   ASSERT_EQ(*p++, '\n');

   for (int i = 0; i < TEST_TERM_ROWS; i++) {

      while (*p == ' ') p++;
      ASSERT_EQ(*p++, '|');
      for (int j = 0; j < TEST_TERM_COLS; j++) {

         char have = vgaentry_get_char(test_video_framebuffer[i][j]);
         char expected = *p++;

         if (expected != '$') {

            if (cursor_row == i && cursor_col == j) {
               /* We didn't expect the cursor, but it's there */
               FAIL() << "Unexpected cursor at row " << i+1 << ", col " << j+1;
            }

            ASSERT_EQ(have, expected)
               << "WRONG char at row " << i+1 << ", col " << j+1;

         } else {

            /* $ stands for the cursor */
            ASSERT_FALSE(cursor_checked)
               << "BAD TEST: cannot have multiple cursors on the screen";

            ASSERT_TRUE(cursor_enabled);
            ASSERT_EQ(cursor_row, i) << "Cursor ROW does not match";
            ASSERT_EQ(cursor_col, j) << "Cursor COL does not match";
            cursor_checked = true;
         }
      }
      ASSERT_EQ(*p++, '|');
      ASSERT_EQ(*p++, '\n');
   }

   /* Test check that expected screen border is correct */
   while (*p == ' ') p++;
   ASSERT_EQ(*p++, '+');
   for (int i = 0; i < TEST_TERM_COLS; i++) {
      ASSERT_EQ(*p++, '-');
   }
   ASSERT_EQ(*p++, '+');
   ASSERT_EQ(*p++, '\n');
}

static void check_color_at(int row, int col, u8 expected_color)
{
   ASSERT_LT(row, TEST_TERM_ROWS);
   ASSERT_LT(col, TEST_TERM_COLS);

   u16 entry = test_video_framebuffer[row][col];
   u8 actual = vgaentry_get_color(entry);

   ASSERT_EQ(actual, expected_color)
      << "Color mismatch at row " << row << ", col " << col
      << " (fg=" << (int)get_color_fg(actual)
      << " bg=" << (int)get_color_bg(actual)
      << " vs expected fg=" << (int)get_color_fg(expected_color)
      << " bg=" << (int)get_color_bg(expected_color) << ")";
}

static void test_vi_set_char_at(u16 row, u16 col, u16 entry)
{
   ASSERT_LT(row, TEST_TERM_ROWS);
   ASSERT_LT(col, TEST_TERM_COLS);

   test_video_framebuffer[row][col] = entry;
}

static void test_vi_set_row(u16 row, u16 *data, bool fpu_allowed)
{
   ASSERT_LT(row, TEST_TERM_ROWS);

   memcpy(&test_video_framebuffer[row],
          data,
          TEST_TERM_COLS * sizeof(u16));
}

static void test_vi_clear_row(u16 row, u8 color)
{
   ASSERT_LT(row, TEST_TERM_ROWS);

   memset(&test_video_framebuffer[row],
          make_vgaentry(' ', color),
          TEST_TERM_COLS * sizeof(u16));
}

static void test_vi_move_cursor(u16 row, u16 col, int color)
{
   ASSERT_LT(row, TEST_TERM_ROWS);
   ASSERT_LT(col, TEST_TERM_COLS);
   cursor_row = row;
   cursor_col = col;
}

static void test_vi_enable_cursor(void)
{
   cursor_enabled = true;
}

static void test_vi_disable_cursor(void)
{
   cursor_enabled = false;
}

static const struct video_interface test_console_vi =
{
   test_vi_set_char_at,
   test_vi_set_row,
   test_vi_clear_row,
   test_vi_move_cursor,
   test_vi_enable_cursor,
   test_vi_disable_cursor,
   NULL, /* textmode_scroll_one_line_up */
   NULL, /* redraw_static_elements */
   NULL, /* disable_static_elems_refresh */
   NULL, /* enable_static_elems_refresh */
};

class console_test : public Test {
public:

   void SetUp() override {
      init_kmalloc_for_tests();
      suppress_printk = true;
      init_first_video_term(&test_console_vi,
                            TEST_TERM_ROWS,
                            TEST_TERM_COLS, -1);
      suppress_printk = false;
      t = allocate_and_init_tty(1, false, 0);
      ASSERT_NE(t, nullptr);
      tty_input_init(t);
      tty_update_default_state_tables(t);
   }

   void TearDown() override {
      __curr_term_intf = nullptr;
   }

   void console_write(const char *buf, size_t n) {
      t->tintf->write(t->tstate, buf, n, t->curr_color);
   }

   void console_write(const char *buf) {
      console_write(buf, strlen(buf));
   }

   void set_cursor_to(int row, int col) {
      char buf[16];
      int n = snprintf(buf, sizeof(buf), "\033[%d;%dH", row + 1, col + 1);
      console_write(buf, (size_t)n);
   }

   size_t drain_tty_input(char *out, size_t maxlen) {
      size_t count = 0;
      u8 b;
      while (count < maxlen && ringbuf_read_elem1(&t->input_ringbuf, &b))
         out[count++] = (char)b;
      return count;
   }

   /*
    * Pre-fill the screen with a known pattern so erase/insert/delete tests
    * can see the effect clearly. Uses uppercase letters, one per cell,
    * wrapping A..Z..A.. The last cell is intentionally left blank so the
    * cursor advance at the end of the fill doesn't trigger a scroll.
    */
   void fill_screen_pattern() {
      /*
       * Emit all cells in ONE write so the vterm's end-of-write move_cursor
       * call sees a safe position (the last cell is skipped for that reason).
       */
      const int last = TEST_TERM_ROWS * TEST_TERM_COLS - 1;
      char buf[TEST_TERM_ROWS * TEST_TERM_COLS];
      for (int i = 0; i < last; i++)
         buf[i] = (char)('A' + (i % 26));
      console_write(buf, (size_t)last);
      console_write("\033[1;1H");
   }

public:
   struct tty *t;
};

TEST_F(console_test, hello)
{
   console_write("hello");
   console_test_dump_screen(true);
   check_screen_vs_expected(R"(
      +--------------------+
      |hello$              |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, wraparound)
{
   console_write("this is a longer string which will wrap around");
   console_test_dump_screen(true);
   check_screen_vs_expected(R"(
      +--------------------+
      |this is a longer str|
      |ing which will wrap |
      |around$             |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, cursor_move)
{
   console_write("this is a longer string which will wrap around");
   console_write("\033[1;1H");
   console_test_dump_screen(true);
   check_screen_vs_expected(R"(
      +--------------------+
      |$his is a longer str|
      |ing which will wrap |
      |around              |
      |                    |
      |                    |
      +--------------------+
   )");
}

/* =============================================================== */
/* Cluster 1: default-state control characters                     */
/* =============================================================== */

TEST_F(console_test, ctrl_cr)
{
   console_write("abc\r");
   check_screen_vs_expected(R"(
      +--------------------+
      |$bc                 |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_lf_no_onlcr)
{
   t->c_term.c_oflag &= ~(tcflag_t)(OPOST | ONLCR);
   console_write("abc\n");
   check_screen_vs_expected(R"(
      +--------------------+
      |abc                 |
      |   $                |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_lf_with_onlcr)
{
   /* OPOST|ONLCR are set in default_termios */
   console_write("abc\n");
   check_screen_vs_expected(R"(
      +--------------------+
      |abc                 |
      |$                   |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_backspace)
{
   /* '\b' only moves the cursor; does not erase. */
   console_write("abc\b");
   check_screen_vs_expected(R"(
      +--------------------+
      |ab$                 |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_tab)
{
   /* tabsize is 8; tab from col 0 advances cursor to col 8. */
   console_write("\t");
   check_screen_vs_expected(R"(
      +--------------------+
      |        $           |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_form_feed)
{
   /* '\f' is treated as a raw linefeed: move down, preserve col. */
   console_write("abc\f");
   check_screen_vs_expected(R"(
      +--------------------+
      |abc                 |
      |   $                |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_vertical_tab)
{
   console_write("abc\v");
   check_screen_vs_expected(R"(
      +--------------------+
      |abc                 |
      |   $                |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_bell_ignored)
{
   console_write("a\ab");
   check_screen_vs_expected(R"(
      +--------------------+
      |ab$                 |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_so_si_switch_charset)
{
   /*
    * Default tables: G0 = ASCII (identity), G1 = graphics (box-drawing).
    * SO (\016) selects G1, SI (\017) selects G0. The character 'q' is
    * an identity under G0 but is a different glyph under G1.
    */
   console_write("q");        /* (0,0): 'q' under G0 */
   console_write("\016q");    /* SO + q => G1 translation at (0,1) */
   console_write("\017q");    /* SI + q => back to G0 at (0,2) */

   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][0]), 'q');
   ASSERT_NE(vgaentry_get_char(test_video_framebuffer[0][1]), 'q');
   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][2]), 'q');
}

TEST_F(console_test, ctrl_verase_deletes_prev_char)
{
   /* VERASE (0x7f by default) is mapped to del_prev_char. */
   const char seq[] = {'a', 'b', 'c', 0x7f, '\0'};
   console_write(seq);
   check_screen_vs_expected(R"(
      +--------------------+
      |ab$                 |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_vwerase_deletes_prev_word)
{
   /* VWERASE (0x17 by default) = Ctrl+W: deletes the last word. */
   const char seq[] = {'a', 'b', ' ', 'c', 'd', 0x17, '\0'};
   console_write(seq);
   check_screen_vs_expected(R"(
      +--------------------+
      |ab $                |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, ctrl_vkill_is_noop)
{
   /* VKILL (0x15) handler is intentionally a no-op; 'X' still renders. */
   const char seq[] = {'a', 'b', 'c', 0x15, 'X', '\0'};
   console_write(seq);
   check_screen_vs_expected(R"(
      +--------------------+
      |abcX$               |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

/* =============================================================== */
/* Cluster 2: cursor-movement CSI sequences                        */
/* =============================================================== */

TEST_F(console_test, csi_cuu_cud_cuf_cub)
{
   /* Start at (0,0), go down 2, right 5 -> (2,5) -> up 1 -> (1,5) -> left 3 -> (1,2) */
   console_write("\033[2B");   /* CUD: down 2 */
   console_write("\033[5C");   /* CUF: right 5 */
   console_write("\033[A");    /* CUU: up 1 (default param) */
   console_write("\033[3D");   /* CUB: left 3 */
   check_screen_vs_expected(R"(
      +--------------------+
      |                    |
      |  $                 |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_cuu_clamp_at_top)
{
   console_write("\033[99A");  /* way more than 5 rows */
   check_screen_vs_expected(R"(
      +--------------------+
      |$                   |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_cuf_clamp_at_right)
{
   console_write("\033[99C");  /* move right by a huge amount; clamps at col 19 */
   check_screen_vs_expected(R"(
      +--------------------+
      |                   $|
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_cup_H_explicit_and_default)
{
   console_write("\033[3;5H");  /* CUP row 3, col 5 (1-based) -> (2, 4) */
   console_write("x");
   console_write("\033[H");     /* CUP with no params -> (0, 0) */
   check_screen_vs_expected(R"(
      +--------------------+
      |$                   |
      |                    |
      |    x               |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_cup_f_equivalent_to_H)
{
   console_write("\033[2;4fZ");  /* `f` is alias for `H` */
   check_screen_vs_expected(R"(
      +--------------------+
      |                    |
      |   Z$               |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_cha_G_and_backtick_same)
{
   /* Write 'X' at row 0 col 5 via CHA (G), then 'Y' at row 0 col 15 via HPA (`). */
   console_write("\033[6GX");
   console_write("\033[16`Y");
   check_screen_vs_expected(R"(
      +--------------------+
      |     X         Y$   |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_vpa_d)
{
   /* Move to row 3 (1-based) keeping col 0, write 'X'. */
   console_write("\033[3dX");
   check_screen_vs_expected(R"(
      +--------------------+
      |                    |
      |                    |
      |X$                  |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_cnl_cpl_EF)
{
   /* CNL(E) moves down N and col=0; CPL(F) moves up N and col=0. */
   console_write("\033[3;5HX"); /* (2,4) = X, cursor (2,5) */
   console_write("\033[E");     /* down 1, col 0 -> (3, 0) */
   console_write("A");
   console_write("\033[2F");    /* up 2, col 0 -> (1, 0) */
   console_write("B");
   check_screen_vs_expected(R"(
      +--------------------+
      |                    |
      |B$                  |
      |    X               |
      |A                   |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_hpr_vpr_ae)
{
   /* a = HPR move right N cols; e = VPR move down N rows. */
   console_write("\033[3a");   /* right 3 -> (0, 3) */
   console_write("X");          /* write X at (0,3), cursor (0,4) */
   console_write("\033[2e");   /* down 2 -> (2, 4) */
   console_write("Y");          /* write Y at (2,4), cursor (2,5) */
   check_screen_vs_expected(R"(
      +--------------------+
      |   X                |
      |                    |
      |    Y$              |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_save_restore_cursor_su)
{
   console_write("ab");
   console_write("\033[s");       /* save */
   console_write("\033[4;10H");   /* jump far */
   console_write("Z");
   console_write("\033[u");       /* restore -> cursor back where 'c' should go */
   console_write("c");
   check_screen_vs_expected(R"(
      +--------------------+
      |abc$                |
      |                    |
      |                    |
      |         Z          |
      |                    |
      +--------------------+
   )");
}

/* =============================================================== */
/* Cluster 3: erase (ED, EL, ECH)                                  */
/* =============================================================== */

TEST_F(console_test, csi_ed_0_J_clears_to_end_of_display)
{
   fill_screen_pattern();
   console_write("\033[2;5H");  /* cursor at (1, 4) */
   console_write("\033[0J");    /* erase from cursor to end of display */
   /* row 0 intact; row 1 cleared from col 4 on; rows 2-4 cleared */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |UVWX$               |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_ed_1_J_clears_to_start_of_display)
{
   fill_screen_pattern();
   console_write("\033[2;5H");  /* cursor at (1, 4) */
   console_write("\033[1J");    /* erase from start of display to cursor */
   /* row 0 cleared; row 1 cols 0..3 cleared (cursor col excluded); rest intact */
   check_screen_vs_expected(R"(
      +--------------------+
      |                    |
      |    $ZABCDEFGHIJKLMN|
      |OPQRSTUVWXYZABCDEFGH|
      |IJKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      +--------------------+
   )");
}

TEST_F(console_test, csi_ed_2_J_clears_all)
{
   fill_screen_pattern();
   console_write("\033[3;5H");
   console_write("\033[2J");    /* erase whole display; cursor preserved */
   check_screen_vs_expected(R"(
      +--------------------+
      |                    |
      |                    |
      |    $               |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_el_0_K_clears_to_eol)
{
   fill_screen_pattern();
   console_write("\033[2;5H");
   console_write("\033[0K");    /* clear current line from cursor to EOL */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |UVWX$               |
      |OPQRSTUVWXYZABCDEFGH|
      |IJKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      +--------------------+
   )");
}

TEST_F(console_test, csi_el_1_K_clears_sol_to_cursor)
{
   fill_screen_pattern();
   console_write("\033[2;5H");
   console_write("\033[1K");   /* clear row 1 cols 0..3 (cursor col excluded) */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |    $ZABCDEFGHIJKLMN|
      |OPQRSTUVWXYZABCDEFGH|
      |IJKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      +--------------------+
   )");
}

TEST_F(console_test, csi_el_2_K_clears_whole_line)
{
   fill_screen_pattern();
   console_write("\033[2;5H");
   console_write("\033[2K");   /* clear whole line */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |    $               |
      |OPQRSTUVWXYZABCDEFGH|
      |IJKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      +--------------------+
   )");
}

TEST_F(console_test, csi_ech_X_erases_chars)
{
   fill_screen_pattern();
   console_write("\033[2;5H");
   console_write("\033[3X");   /* erase 3 chars from cursor (does not move cursor) */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |UVWX$  BCDEFGHIJKLMN|
      |OPQRSTUVWXYZABCDEFGH|
      |IJKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      +--------------------+
   )");
}

/* =============================================================== */
/* Cluster 4: insert/delete (ICH, DCH, IL, DL)                     */
/* =============================================================== */

TEST_F(console_test, csi_ich_at_inserts_blanks)
{
   fill_screen_pattern();
   console_write("\033[2;5H");
   console_write("\033[3@");   /* insert 3 blanks at cursor; rest shifts right */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |UVWX$  YZABCDEFGHIJK|
      |OPQRSTUVWXYZABCDEFGH|
      |IJKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      +--------------------+
   )");
}

TEST_F(console_test, csi_dch_P_deletes_chars)
{
   fill_screen_pattern();
   console_write("\033[2;5H");
   console_write("\033[3P");
   /*
    * Row 1 starts as "UVWXYZABCDEFGHIJKLMN". Cursor at col 4 (Y).
    * DCH 3 deletes 3 chars at cursor: drop YZA, shift BCD..MN left, blank
    * the trailing 3 cells (cols 17..19).
    */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |UVWX$CDEFGHIJKLMN   |
      |OPQRSTUVWXYZABCDEFGH|
      |IJKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      +--------------------+
   )");
}

TEST_F(console_test, csi_il_L_inserts_blank_lines)
{
   fill_screen_pattern();
   console_write("\033[2;1H");  /* cursor at (1, 0) */
   console_write("\033[2L");    /* insert 2 blank lines at cursor */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |$                   |
      |                    |
      |UVWXYZABCDEFGHIJKLMN|
      |OPQRSTUVWXYZABCDEFGH|
      +--------------------+
   )");
}

TEST_F(console_test, csi_dl_M_deletes_lines)
{
   fill_screen_pattern();
   console_write("\033[2;1H");
   console_write("\033[2M");    /* delete 2 lines from cursor; rest shifts up */
   check_screen_vs_expected(R"(
      +--------------------+
      |ABCDEFGHIJKLMNOPQRST|
      |$JKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      |                    |
      |                    |
      +--------------------+
   )");
}

/* =============================================================== */
/* Cluster 5: scroll (SU, SD, DECSTBM)                             */
/* =============================================================== */

TEST_F(console_test, csi_su_S_scrolls_up)
{
   fill_screen_pattern();
   console_write("\033[2S");   /* scroll up 2: top 2 rows disappear, new blanks at bottom */
   console_write("\033[1;1H");
   check_screen_vs_expected(R"(
      +--------------------+
      |$PQRSTUVWXYZABCDEFGH|
      |IJKLMNOPQRSTUVWXYZAB|
      |CDEFGHIJKLMNOPQRSTU |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_sd_T_scrolls_down)
{
   fill_screen_pattern();
   console_write("\033[2T");   /* scroll down 2: bottom 2 rows disappear, new blanks at top */
   console_write("\033[1;1H");
   check_screen_vs_expected(R"(
      +--------------------+
      |$                   |
      |                    |
      |ABCDEFGHIJKLMNOPQRST|
      |UVWXYZABCDEFGHIJKLMN|
      |OPQRSTUVWXYZABCDEFGH|
      +--------------------+
   )");
}

TEST_F(console_test, csi_scroll_region_r_invalid_ignored)
{
   /*
    * tty_csi_r_handler guards with s >= e and silently ignores. Use (5,2):
    * s = 4, e = 1 -> s > e -> no-op. Screen stays default.
    */
   console_write("\033[5;2r");
   console_write("X");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_scroll_region_r_valid_homes_cursor)
{
   /*
    * set_scroll_region moves the cursor to (0, 0) as a side-effect.
    * Start with a non-zero cursor, issue a valid region, then verify
    * that the next write lands at (0, 0).
    */
   console_write("\033[3;5Habc");    /* cursor at (2, 7) */
   console_write("\033[2;4r");        /* set region rows 2..4 (1-based) */
   console_write("X");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |    abc             |
      |                    |
      |                    |
      +--------------------+
   )");
}

/* =============================================================== */
/* Cluster 6: SGR (colors and attributes)                          */
/* =============================================================== */

TEST_F(console_test, sgr_reset_0)
{
   console_write("\033[31m");  /* set fg red */
   console_write("\033[0mX");  /* reset, write X */
   check_color_at(0, 0, DEFAULT_COLOR16);
}

TEST_F(console_test, sgr_no_params_equals_reset)
{
   console_write("\033[31m");  /* set fg red */
   console_write("\033[mX");   /* empty params == 0 == reset */
   check_color_at(0, 0, DEFAULT_COLOR16);
}

TEST_F(console_test, sgr_bold_1)
{
   /* Bold brightens fg: fg=7 (white) -> fg=15 (bright white). */
   console_write("\033[1mX");
   check_color_at(0, 0, make_color(COLOR_BRIGHT_WHITE, COLOR_BLACK));
}

TEST_F(console_test, sgr_reverse_7)
{
   /* Reverse swaps fg/bg. */
   console_write("\033[7mX");
   check_color_at(0, 0, make_color(COLOR_BLACK, COLOR_WHITE));
}

TEST_F(console_test, sgr_fg_colors_30_to_37)
{
   /* Expected fg after each SGR ESC[Nm for N in 30..37. */
   const u8 expected_fg[8] = {
      COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
      COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
   };
   for (int i = 0; i < 8; i++) {
      char seq[16];
      int n = snprintf(seq, sizeof(seq), "\033[%dmX", 30 + i);
      console_write(seq, (size_t)n);
      check_color_at(0, i, make_color(expected_fg[i], COLOR_BLACK));
   }
}

TEST_F(console_test, sgr_fg_bright_90_to_97)
{
   const u8 expected_fg[8] = {
      COLOR_BRIGHT_BLACK, COLOR_BRIGHT_RED, COLOR_BRIGHT_GREEN,
      COLOR_BRIGHT_YELLOW, COLOR_BRIGHT_BLUE, COLOR_BRIGHT_MAGENTA,
      COLOR_BRIGHT_CYAN, COLOR_BRIGHT_WHITE
   };
   for (int i = 0; i < 8; i++) {
      char seq[16];
      int n = snprintf(seq, sizeof(seq), "\033[%dmX", 90 + i);
      console_write(seq, (size_t)n);
      check_color_at(0, i, make_color(expected_fg[i], COLOR_BLACK));
   }
}

TEST_F(console_test, sgr_bg_colors_40_to_47)
{
   const u8 expected_bg[8] = {
      COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
      COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
   };
   for (int i = 0; i < 8; i++) {
      char seq[16];
      int n = snprintf(seq, sizeof(seq), "\033[%dmX", 40 + i);
      console_write(seq, (size_t)n);
      check_color_at(0, i, make_color(COLOR_WHITE, expected_bg[i]));
   }
}

TEST_F(console_test, sgr_bg_bright_100_to_107)
{
   const u8 expected_bg[8] = {
      COLOR_BRIGHT_BLACK, COLOR_BRIGHT_RED, COLOR_BRIGHT_GREEN,
      COLOR_BRIGHT_YELLOW, COLOR_BRIGHT_BLUE, COLOR_BRIGHT_MAGENTA,
      COLOR_BRIGHT_CYAN, COLOR_BRIGHT_WHITE
   };
   for (int i = 0; i < 8; i++) {
      char seq[16];
      int n = snprintf(seq, sizeof(seq), "\033[%dmX", 100 + i);
      console_write(seq, (size_t)n);
      check_color_at(0, i, make_color(COLOR_WHITE, expected_bg[i]));
   }
}

TEST_F(console_test, sgr_reset_fg_39)
{
   console_write("\033[31m");       /* fg red */
   console_write("\033[39mX");      /* reset fg to default */
   check_color_at(0, 0, DEFAULT_COLOR16);
}

TEST_F(console_test, sgr_reset_bg_49)
{
   console_write("\033[41m");       /* bg red */
   console_write("\033[49mX");      /* reset bg to default */
   check_color_at(0, 0, DEFAULT_COLOR16);
}

TEST_F(console_test, sgr_multiple_params)
{
   /* ESC[1;31;42m applies bold, fg=red, bg=green in one sequence. */
   console_write("\033[1;31;42mX");
   check_color_at(0, 0, make_color(COLOR_BRIGHT_RED, COLOR_GREEN));
}

TEST_F(console_test, sgr_unknown_param_is_noop)
{
   /* 99 is not in any handled range: current color is unchanged. */
   console_write("\033[99mX");
   check_color_at(0, 0, DEFAULT_COLOR16);
}

/* =============================================================== */
/* Cluster 7: ESC non-CSI sequences                                */
/* =============================================================== */

TEST_F(console_test, esc_c_RIS_full_reset)
{
   /* Put the terminal in a non-default state: colored text, moved cursor. */
   console_write("\033[31m");          /* fg red */
   console_write("\033[3;7Hhello");    /* move cursor + write */
   console_write("\033c");             /* RIS: full reset */
   console_write("X");                  /* should land at (0,0) with default color */

   check_color_at(0, 0, DEFAULT_COLOR16);
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, esc_D_IND_default_onlcr)
{
   /*
    * ESC D (IND) is index: cursor goes one line down, column preserved.
    * OPOST|ONLCR is termios output processing for raw '\n' bytes; IND
    * must bypass it.
    */
   console_write("ab");
   console_write("\033D");
   console_write("X");
   check_screen_vs_expected(R"(
      +--------------------+
      |ab                  |
      |  X$                |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, esc_D_IND_no_onlcr_preserves_col)
{
   /* Same as above but with ONLCR cleared: behavior is identical. */
   t->c_term.c_oflag &= ~(tcflag_t)(OPOST | ONLCR);
   console_write("ab");
   console_write("\033D");
   console_write("X");
   check_screen_vs_expected(R"(
      +--------------------+
      |ab                  |
      |  X$                |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, esc_M_RI_middle_moves_cursor_up)
{
   console_write("\033[3;5Hab");   /* cursor at (2, 6) */
   console_write("\033M");         /* RI: up one line (not at top) */
   console_write("X");
   check_screen_vs_expected(R"(
      +--------------------+
      |                    |
      |      X$            |
      |    ab              |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, esc_M_RI_top_scrolls)
{
   /* At top row, RI scrolls the content down by one row. */
   console_write("ABCDEFGHIJ");   /* row 0: "ABCDEFGHIJ" + spaces */
   console_write("\033[1;1H");    /* cursor back to (0, 0) */
   console_write("\033M");        /* RI at top: scroll down */
   /* Row 0 becomes blank, row 1 now has the original row 0 content. */
   check_screen_vs_expected(R"(
      +--------------------+
      |$                   |
      |ABCDEFGHIJ          |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

/* =============================================================== */
/* Cluster 8: character-set selection                              */
/* =============================================================== */

TEST_F(console_test, charset_g0_select_graphics)
{
   /* ESC ( 0 selects the graphics (line-drawing) table for G0. */
   console_write("q");           /* G0 = ASCII: renders 'q' */
   console_write("\033(0");      /* select graphics for G0 */
   console_write("q");           /* now 'q' maps to a box-drawing glyph */
   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][0]), 'q');
   ASSERT_NE(vgaentry_get_char(test_video_framebuffer[0][1]), 'q');
}

TEST_F(console_test, charset_g0_select_ascii)
{
   /* Switch to graphics, then back to ASCII. */
   console_write("\033(0");
   console_write("q");              /* graphics */
   console_write("\033(B");         /* back to ASCII */
   console_write("q");              /* identity */
   ASSERT_NE(vgaentry_get_char(test_video_framebuffer[0][0]), 'q');
   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][1]), 'q');
}

TEST_F(console_test, charset_g1_graphics_with_so)
{
   /*
    * G1 defaults to the graphics table. ESC ) 0 re-asserts that.
    * SO (0x0E) selects G1; SI (0x0F) selects G0.
    */
   console_write("\033)0");        /* set G1 = graphics */
   console_write("q");             /* still G0 = ASCII */
   console_write("\016q");         /* SO + q -> G1 graphics */
   console_write("\017q");         /* SI + q -> G0 ASCII */
   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][0]), 'q');
   ASSERT_NE(vgaentry_get_char(test_video_framebuffer[0][1]), 'q');
   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][2]), 'q');
}

TEST_F(console_test, charset_unknown_letter_noop)
{
   /* ESC ( U and ESC ( K are recognized but leave the current table unchanged. */
   console_write("q");               /* ASCII 'q' */
   console_write("\033(U");          /* no-op */
   console_write("q");               /* still ASCII 'q' */
   console_write("\033(K");          /* no-op */
   console_write("q");               /* still ASCII 'q' */
   for (int i = 0; i < 3; i++)
      ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][i]), 'q');
}

/* =============================================================== */
/* Cluster 9: private CSI extensions                               */
/* =============================================================== */

TEST_F(console_test, csi_pvt_cursor_hide_show_25)
{
   console_write("\033[?25l");
   ASSERT_FALSE(cursor_enabled);
   console_write("\033[?25h");
   ASSERT_TRUE(cursor_enabled);
}

TEST_F(console_test, csi_pvt_c_is_ignored)
{
   /* ESC [ ? 6 c is a Linux-specific cursor-appearance hint; handler returns early. */
   console_write("\033[?6c");
   console_write("X");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, csi_pvt_alt_buffer_1049)
{
   console_write("MAIN");            /* main: "MAIN", cursor (0, 4) */
   console_write("\033[?1049h");     /* enter alt buffer: snapshot is saved */
   console_write("\033[1;1HALT");    /* overwrite chars at (0,0..2) with "ALT" */

   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][0]), 'A');
   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][1]), 'L');
   ASSERT_EQ(vgaentry_get_char(test_video_framebuffer[0][2]), 'T');

   console_write("\033[?1049l");     /* leave alt: snapshot restored */

   check_screen_vs_expected(R"(
      +--------------------+
      |MAIN$               |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

/* =============================================================== */
/* Cluster 10: device status reports                               */
/* =============================================================== */

/*
 * __disable_preempt starts at 1 in test binaries (init_sched isn't called),
 * so tty_inbuf_write_elem's ASSERT on preemption trips when a CSI handler
 * sends reply bytes via tty_send_keyevent(block=true). The three reply-path
 * tests below flip preemption on briefly, then restore it so subsequent
 * tests' SetUp (which calls kmalloc under an ASSERT(!is_preemption_enabled))
 * continues to work.
 */
class console_test_reply_path : public console_test {
public:
   void SetUp() override {
      console_test::SetUp();
      force_enable_preemption();
      /* Disable ECHO so response bytes don't write to the screen too. */
      t->c_term.c_lflag &= ~(tcflag_t)ECHO;
   }

   void TearDown() override {
      disable_preemption();  /* restore count to 1 to mirror boot state */
      console_test::TearDown();
   }
};

TEST_F(console_test_reply_path, dsr_cpr_6n_reports_cursor_position)
{
   console_write("\033[2;5H");     /* cursor at (1, 4); 1-based (2, 5) */
   console_write("\033[6n");       /* CPR */

   char buf[32] = {0};
   size_t n = drain_tty_input(buf, sizeof(buf) - 1);
   ASSERT_STREQ(buf, "\033[2;5R");
   ASSERT_EQ(n, 6u);
}

TEST_F(console_test_reply_path, dsr_status_5n_reports_ok)
{
   console_write("\033[5n");

   char buf[32] = {0};
   size_t n = drain_tty_input(buf, sizeof(buf) - 1);
   ASSERT_STREQ(buf, "\033[0n");
   ASSERT_EQ(n, 4u);
}

TEST_F(console_test_reply_path, csi_da_c_reports_vt102)
{
   console_write("\033[c");        /* device attributes query */

   char buf[32] = {0};
   size_t n = drain_tty_input(buf, sizeof(buf) - 1);
   ASSERT_STREQ(buf, "\033[?6c");
   ASSERT_EQ(n, 5u);
}

/* =============================================================== */
/* Cluster 11: state-machine edge cases                            */
/* =============================================================== */

TEST_F(console_test, sm_esc_interrupts_csi)
{
   /*
    * Start a partial CSI (ESC [ 3), then send a new ESC before any final
    * byte. tty_pre_filter aborts the old sequence and starts fresh.
    */
   console_write("\033[3");         /* partial CSI */
   console_write("\033[1;1HX");     /* new CUP wins, write X at (0,0) */
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_unknown_csi_final_byte_silently_dropped)
{
   /* 'z' has no handler; the sequence is silently consumed. */
   console_write("\033[z");
   console_write("X");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_csi_alt_byte_9b)
{
   /* 0x9B is the alternate single-byte CSI introducer. */
   const char seq[] = {'\x9b', '3', ';', '5', 'H', 'X', '\0'};
   console_write(seq);
   check_screen_vs_expected(R"(
      +--------------------+
      |                    |
      |                    |
      |    X$              |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_unknown_esc_seq_consumed)
{
   /*
    * ESC X: X (0x58) is inside [0x40, 0x5F], so tty_state_esc2_unknown
    * terminates immediately after X. The next 'A' renders normally.
    */
   console_write("\033X");
   console_write("A");
   check_screen_vs_expected(R"(
      +--------------------+
      |A$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_param_bytes_overflow_recovers)
{
   /*
    * param_bytes[] is 64 bytes; write 70 params to trigger overflow. The
    * sequence is silently dropped and the filter returns to default. We
    * then reset the screen via CLR + HOME, and write X to confirm the
    * filter has recovered correctly.
    */
   std::string s = "\033[";
   for (int i = 0; i < 70; i++)
      s += "1;";
   s += "m";
   console_write(s.c_str(), s.size());

   console_write("\033[2J\033[1;1HX");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_unknown_private_csi_extension)
{
   /* ESC [ ? <n> h/l where n isn't 25 or 1049 hits the default arm. */
   console_write("\033[?999h");
   console_write("X");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_esc_interrupts_esc1)
{
   /* ESC then ESC (no intermediate): pre_filter in esc1 should restart. */
   console_write("\033\033[1;1HX");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_esc_interrupts_unknown_esc)
{
   /*
    * ESC <non-terminator>: enter esc2_unknown with a byte outside
    * [0x40, 0x5F], then another ESC starts a fresh sequence via pre_filter.
    */
   console_write("\033~");                 /* ESC ~ — ~ is 0x7E, not terminator */
   console_write("\033[1;1HX");             /* new ESC aborts unknown, CUP + X */
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_esc_interrupts_charset_selection)
{
   /* ESC ( opens G0 charset state; a second ESC restarts. */
   console_write("\033(");              /* enter esc2_par0 */
   console_write("\033[1;1HX");          /* new ESC aborts, CUP + X */
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_esc_interrupts_g1_charset_selection)
{
   console_write("\033)");              /* enter esc2_par1 */
   console_write("\033[1;1HX");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}

TEST_F(console_test, sm_interm_bytes_overflow_recovers)
{
   /* interm_bytes[] is 64 bytes; 70 intermediate bytes overflow. */
   std::string s = "\033[";
   for (int i = 0; i < 70; i++)
      s += ' ';
   s += "m";
   console_write(s.c_str(), s.size());

   console_write("\033[2J\033[1;1HX");
   check_screen_vs_expected(R"(
      +--------------------+
      |X$                  |
      |                    |
      |                    |
      |                    |
      |                    |
      +--------------------+
   )");
}
