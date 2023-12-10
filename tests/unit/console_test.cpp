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
   #include <tilck/common/color_defs.h>
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
