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
