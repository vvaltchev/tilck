/* SPDX-License-Identifier: BSD-2-Clause */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
   #include <tilck/common/basic_defs.h>
   #include <tilck/common/page_size.h>
   #include <tilck/kernel/user.h>
   #include <tilck/kernel/sched.h>

   /*
    * copy_str_from_user() is gmock-wrapped in this build, so call the real one
    * via __real_; copy_str_array_from_user() is not wrapped (call it directly).
    */
   int __real_copy_str_from_user(void *, const void *, size_t, size_t *);

   /* Fault-injection knob of the arch_user_copy() fake (asm_fake_funcs.c). */
   void set_user_copy_fault(ulong lo, ulong hi, int fault_after);

   extern void *base_va;
   extern struct task *__current;
}

using namespace std;

/*
 * The string copiers classify a pointer as "user" when it is below BASE_VA
 * (== base_va in test builds) and rely on arch_user_copy() to fault on an
 * unmapped page. We set base_va above every real host address, so ordinary
 * heap/stack buffers act as user memory, and drive faults through the fake.
 */
class user_copy_str : public ::testing::Test {
protected:

   void *saved_base_va;
   struct task *saved_current;
   struct task tsk;

   /* Aligned scratch: a user argv in `ubuf`, the kernel output in `dbuf`. */
   alignas(void *) char ubuf[512];
   alignas(void *) char dbuf[512];

   void SetUp() override {
      saved_base_va = base_va;
      saved_current = __current;

      base_va = (void *)(1ul << 60);    /* every real address is "user" */
      memset(&tsk, 0, sizeof(tsk));     /* user_access_fixup == NULL */
      __current = &tsk;
      set_user_copy_fault(0, 0, -1);    /* no fault by default */
   }

   void TearDown() override {
      set_user_copy_fault(0, 0, -1);
      __current = saved_current;
      base_va = saved_base_va;
   }

   /*
    * Lay out a user argv in ubuf: the pointer array first, then the strings it
    * points to. Returns the (user) address of the pointer array; with base_va
    * high, the real addresses stored in it are valid user pointers.
    */
   const char *const *build_argv(const vector<const char *> &args) {
      char **arr = (char **)(void *) ubuf;
      size_t soff = (args.size() + 1) * sizeof(char *);

      for (size_t i = 0; i < args.size(); i++) {
         strcpy(ubuf + soff, args[i]);
         arr[i] = ubuf + soff;
         soff += strlen(args[i]) + 1;
      }

      arr[args.size()] = nullptr;
      return (const char *const *) arr;
   }
};

/* --- copy_str_from_user() / internal_copy_user_str() --- */

TEST_F(user_copy_str, basic)
{
   char dst[64];
   size_t w = 0;
   int rc = __real_copy_str_from_user(dst, "hello", sizeof(dst), &w);

   EXPECT_EQ(rc, 0);
   EXPECT_STREQ(dst, "hello");
   EXPECT_EQ(w, 6u);                    /* "hello" + the NUL */
}

TEST_F(user_copy_str, null_written_ptr)
{
   char dst[16];
   int rc = __real_copy_str_from_user(dst, "hi", sizeof(dst), nullptr);

   EXPECT_EQ(rc, 0);
   EXPECT_STREQ(dst, "hi");
}

TEST_F(user_copy_str, multi_chunk)
{
   /* > USER_STR_COPY_CHUNK (64) forces several read chunks */
   const string big(200, 'a');
   char dst[256];
   size_t w = 0;
   int rc = __real_copy_str_from_user(dst, big.c_str(), sizeof(dst), &w);

   EXPECT_EQ(rc, 0);
   EXPECT_STREQ(dst, big.c_str());
   EXPECT_EQ(w, big.size() + 1);
}

TEST_F(user_copy_str, too_small)
{
   char dst[4];
   size_t w = 0;
   int rc = __real_copy_str_from_user(dst, "hello", sizeof(dst), &w);

   EXPECT_EQ(rc, 1);                    /* no room for the whole string */
}

TEST_F(user_copy_str, zero_max_size)
{
   char dst[1];
   size_t w = 0;
   int rc = __real_copy_str_from_user(dst, "x", 0, &w);

   EXPECT_EQ(rc, 1);
}

TEST_F(user_copy_str, ptr_past_base_va)
{
   char dst[16];
   size_t w = 0;
   int rc = __real_copy_str_from_user(dst, base_va, sizeof(dst), &w);

   EXPECT_EQ(rc, -1);                   /* walked off the user space */
}

TEST_F(user_copy_str, fault_on_unmapped_page)
{
   /*
    * A page-aligned buffer with no NUL: the string starts 8 bytes before the
    * page-0/page-1 boundary and page 1 is "unmapped". The first chunk must
    * stop at the page boundary (and succeed), the second must fault.
    */
   char *pg = (char *) aligned_alloc(PAGE_SIZE, 2 * PAGE_SIZE);
   ASSERT_NE(pg, nullptr);
   memset(pg, 'a', 2 * PAGE_SIZE);

   set_user_copy_fault((ulong)(pg + PAGE_SIZE), (ulong)(pg + 2 * PAGE_SIZE), 0);

   char dst[256];
   size_t w = 0;
   int rc = __real_copy_str_from_user(dst, pg + PAGE_SIZE - 8, sizeof(dst), &w);

   EXPECT_EQ(rc, -1);
   free(pg);
}

/* --- copy_str_array_from_user() / internal_copy_str_array_from_user() --- */

TEST_F(user_copy_str, array_basic)
{
   const char *const *argv = build_argv({"a", "bb", "ccc"});
   size_t w = 0;
   int rc = copy_str_array_from_user(dbuf, argv, sizeof(dbuf), &w);
   ASSERT_EQ(rc, 0);

   char **out = (char **)(void *) dbuf;
   EXPECT_STREQ(out[0], "a");
   EXPECT_STREQ(out[1], "bb");
   EXPECT_STREQ(out[2], "ccc");
   EXPECT_EQ(out[3], nullptr);
}

TEST_F(user_copy_str, array_empty)
{
   const char *const *argv = build_argv({});
   size_t w = 0;
   int rc = copy_str_array_from_user(dbuf, argv, sizeof(dbuf), &w);

   ASSERT_EQ(rc, 0);
   EXPECT_EQ(((char **)(void *) dbuf)[0], nullptr);
}

TEST_F(user_copy_str, array_ptr_out_of_range)
{
   /* A pointer array straddling base_va: user_out_of_range() rejects it. */
   const char *const *argv = (const char *const *)(void *)((char *)base_va - 4);
   size_t w = 0;
   int rc = copy_str_array_from_user(dbuf, argv, sizeof(dbuf), &w);

   EXPECT_EQ(rc, -1);
}

TEST_F(user_copy_str, array_ptr_fault)
{
   const char *const *argv = build_argv({"a"});
   size_t w = 0;

   /* The first pointer read faults. */
   set_user_copy_fault((ulong)argv, (ulong)argv + sizeof(void *), 0);
   int rc = copy_str_array_from_user(dbuf, argv, sizeof(dbuf), &w);

   EXPECT_EQ(rc, -1);
}

TEST_F(user_copy_str, array_dest_too_small)
{
   const char *const *argv = build_argv({"a", "b", "c"});
   size_t w = 0;

   /* room for fewer than the 4-pointer array */
   int rc = copy_str_array_from_user(dbuf, argv, 2 * sizeof(void *), &w);
   EXPECT_EQ(rc, 1);
}

TEST_F(user_copy_str, array_string_fault)
{
   const char *const *argv = build_argv({"abc"});
   size_t w = 0;

   /* The argv[0] string itself is "unmapped". */
   set_user_copy_fault((ulong)argv[0], (ulong)argv[0] + 4, 0);
   int rc = copy_str_array_from_user(dbuf, argv, sizeof(dbuf), &w);

   EXPECT_EQ(rc, -1);
}

TEST_F(user_copy_str, array_ptr_reread_fault)
{
   const char *const *argv = build_argv({"a"});
   size_t w = 0;

   /*
    * Let the first read of the pointer (argc loop) through, fault the second
    * (the per-string re-read) -- the only way to reach that defensive branch.
    */
   set_user_copy_fault((ulong)argv, (ulong)argv + sizeof(void *), 1);
   int rc = copy_str_array_from_user(dbuf, argv, sizeof(dbuf), &w);

   EXPECT_EQ(rc, -1);
}
