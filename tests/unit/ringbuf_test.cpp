#include <iostream>
#include <cstdio>
#include <random>
#include <vector>
#include <gtest/gtest.h>

using namespace std;
using namespace testing;

extern "C" {
   #include <tilck/kernel/ringbuf.h>
}

TEST(ringbuf, basicTest)
{
   int buffer[3] = {0};
   int values[] = {1,2,3,4};
   int val;
   struct ringbuf rb;
   bool success;

   ringbuf_init(&rb, ARRAY_SIZE(buffer), sizeof(buffer[0]), buffer);
   ASSERT_TRUE(ringbuf_is_empty(&rb));

   for (int i = 0; i < 3; i++) {
      success = ringbuf_write_elem(&rb, &values[i]);
      ASSERT_TRUE(success);
   }

   success = ringbuf_write_elem(&rb, &values[3]);
   ASSERT_FALSE(success);
   ASSERT_TRUE(ringbuf_is_full(&rb));

   for (int i = 0; i < 3; i++) {
      success = ringbuf_read_elem(&rb, &val);
      ASSERT_TRUE(success);
      ASSERT_EQ(val, values[i]);
   }

   success = ringbuf_read_elem(&rb, &val);
   ASSERT_FALSE(success);
   ASSERT_TRUE(ringbuf_is_empty(&rb));

   ringbuf_destory(&rb);
}

TEST(ringbuf, basicTest_1)
{
   u8 buffer[3] = {0};
   u8 values[] = {1,2,3,4};
   u8 val;
   struct ringbuf rb;
   bool success;

   ringbuf_init(&rb, ARRAY_SIZE(buffer), sizeof(buffer[0]), buffer);
   ASSERT_TRUE(ringbuf_is_empty(&rb));

   for (int i = 0; i < 3; i++) {
      success = ringbuf_write_elem1(&rb, values[i]);
      ASSERT_TRUE(success);
   }

   success = ringbuf_write_elem1(&rb, values[3]);
   ASSERT_FALSE(success);
   ASSERT_TRUE(ringbuf_is_full(&rb));

   for (int i = 0; i < 3; i++) {
      success = ringbuf_read_elem1(&rb, &val);
      ASSERT_TRUE(success);
      ASSERT_EQ(val, values[i]);
   }

   success = ringbuf_read_elem1(&rb, &val);
   ASSERT_FALSE(success);
   ASSERT_TRUE(ringbuf_is_empty(&rb));

   ringbuf_destory(&rb);
}

TEST(ringbuf, rotation)
{
   int buffer[3] = {0};
   int values[] = {1,2,3,4,5,6,7,8,9};
   int val;
   struct ringbuf rb;
   bool success;

   ringbuf_init(&rb, ARRAY_SIZE(buffer), sizeof(buffer[0]), buffer);
   ASSERT_TRUE(ringbuf_is_empty(&rb));

   /* Fill the buffer */
   for (int i = 0; i < 3; i++) {
      success = ringbuf_write_elem(&rb, &values[i]);
      ASSERT_TRUE(success);
   }

   /* Now read 2 elems */
   for (int i = 0; i < 2; i++) {
      success = ringbuf_read_elem(&rb, &val);
      ASSERT_TRUE(success);
      ASSERT_EQ(val, values[i]);
   }

   /* Now write 2 new elems */
   for (int i = 0; i < 2; i++) {
      success = ringbuf_write_elem(&rb, &values[3+i]);
      ASSERT_TRUE(success);
   }

   ASSERT_TRUE(ringbuf_is_full(&rb));

   /* Now read the whole buffer */
   for (int i = 0; i < 3; i++) {
      success = ringbuf_read_elem(&rb, &val);
      ASSERT_TRUE(success);
      ASSERT_EQ(val, values[2+i]);
   }

   ASSERT_TRUE(ringbuf_is_empty(&rb));
   ringbuf_destory(&rb);
}


TEST(ringbuf, rotation_1)
{
   u8 buffer[3] = {0};
   u8 values[] = {1,2,3,4,5,6,7,8,9};
   u8 val;
   struct ringbuf rb;
   bool success;

   ringbuf_init(&rb, ARRAY_SIZE(buffer), sizeof(buffer[0]), buffer);
   ASSERT_TRUE(ringbuf_is_empty(&rb));

   /* Fill the buffer */
   for (int i = 0; i < 3; i++) {
      success = ringbuf_write_elem1(&rb, values[i]);
      ASSERT_TRUE(success);
   }

   /* Now read 2 elems */
   for (int i = 0; i < 2; i++) {
      success = ringbuf_read_elem1(&rb, &val);
      ASSERT_TRUE(success);
      ASSERT_EQ(val, values[i]);
   }

   /* Now write 2 new elems */
   for (int i = 0; i < 2; i++) {
      success = ringbuf_write_elem1(&rb, values[3+i]);
      ASSERT_TRUE(success);
   }

   ASSERT_TRUE(ringbuf_is_full(&rb));

   /* Now read the whole buffer */
   for (int i = 0; i < 3; i++) {
      success = ringbuf_read_elem1(&rb, &val);
      ASSERT_TRUE(success);
      ASSERT_EQ(val, values[2+i]);
   }

   ASSERT_TRUE(ringbuf_is_empty(&rb));
   ringbuf_destory(&rb);
}

TEST(ringbuf, unwrite)
{
   int buffer[3] = {0};
   int values[] = {10, 20, 30};
   int val;
   struct ringbuf rb;
   bool success;

   ringbuf_init(&rb, ARRAY_SIZE(buffer), sizeof(buffer[0]), buffer);
   ASSERT_TRUE(ringbuf_is_empty(&rb));

   /* Fill the buffer */
   for (int i = 0; i < 3; i++) {
      success = ringbuf_write_elem(&rb, &values[i]);
      ASSERT_TRUE(success);
   }

   ASSERT_TRUE(ringbuf_is_full(&rb));

   for (int i = 2; i >= 0; i--) {
      success = ringbuf_unwrite_elem(&rb, &val);
      ASSERT_TRUE(success);
      ASSERT_EQ(val, values[i]) << "[FAIL for i: " << i << "]";
   }

   success = ringbuf_unwrite_elem(&rb, &val);
   ASSERT_FALSE(success);

   ASSERT_TRUE(ringbuf_is_empty(&rb));
   ringbuf_destory(&rb);
}

TEST(ringbuf, read_write_bytes)
{
   struct ringbuf rb;
   char buffer[9] = "--------";
   char rbuf[9] = {0};
   u32 rc;

   ringbuf_init(&rb, 8, 1, buffer);
   ASSERT_TRUE(ringbuf_is_empty(&rb));

   rc = ringbuf_write_bytes(&rb, (u8 *)"12345", 5);
   ASSERT_EQ(rc, 5U);

   ASSERT_STREQ(buffer, "12345---");

   rc = ringbuf_read_bytes(&rb, (u8 *)rbuf, 3);
   ASSERT_EQ(rc, 3U);
   rbuf[rc] = 0;

   ASSERT_STREQ(rbuf, "123");

   rc = ringbuf_write_bytes(&rb, (u8 *)"6789abcdef", 10);
   ASSERT_EQ(rc, 6U);

   ASSERT_STREQ(buffer, "9ab45678");

   rc = ringbuf_read_bytes(&rb, (u8 *)rbuf, 4);
   ASSERT_EQ(rc, 4U);
   rbuf[rc] = 0;

   ASSERT_STREQ(rbuf, "4567");

   rc = ringbuf_write_bytes(&rb, (u8 *)"qwerty", 6);
   ASSERT_EQ(rc, 4U);

   ASSERT_STREQ(buffer, "9abqwer8");

   rc = ringbuf_read_bytes(&rb, (u8 *)rbuf, 8);
   ASSERT_EQ(rc, 8U);
   rbuf[rc] = 0;

   ASSERT_STREQ(rbuf, "89abqwer");

   ASSERT_TRUE(ringbuf_is_empty(&rb));
   ringbuf_destory(&rb);
}
