/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace std;
using namespace testing;

/*
 * -----------------------------------------------------
 * Mocking of C functions
 * -----------------------------------------------------
 */

extern "C" {

   bool experiment_bar(void);
   int experiment_foo(int n);

   bool __real_experiment_bar(void);
   int __real_experiment_foo(int);
};

class ExpGlobalFuncs {

   ExpGlobalFuncs *previous_inst;

public:
   ExpGlobalFuncs() {
      previous_inst = gInstance;
      gInstance = this;
   }

   virtual ~ExpGlobalFuncs() {
      gInstance = previous_inst;
   }

   virtual bool bar() { return __real_experiment_bar(); }
   virtual int foo(int n) { return __real_experiment_foo(n); }

   static ExpGlobalFuncs *gInstance;
};

class MockExpGlobalFuncs : public ExpGlobalFuncs {
public:

   MOCK_METHOD(bool, bar, (), (override));
};

ExpGlobalFuncs *ExpGlobalFuncs::gInstance = new ExpGlobalFuncs;

extern "C" {

bool __wrap_experiment_bar(void) {
   return ExpGlobalFuncs::gInstance->bar();
}

int __wrap_experiment_foo(int n) {
   return ExpGlobalFuncs::gInstance->foo(n);
}

}

TEST(experiment, gfuncs1)
{
   ASSERT_EQ(experiment_bar(), true);
   ASSERT_EQ(experiment_foo(5), 50);
   ASSERT_EQ(experiment_foo(6), 60);
}

TEST(experiment, gfuncs2)
{
   NiceMock<MockExpGlobalFuncs> mock;

   EXPECT_CALL(mock, bar())
      .WillOnce(Return(false))
      .WillOnce(Return(true));

   ASSERT_EQ(experiment_foo(5), -1);
   ASSERT_EQ(experiment_foo(5), 50);
}

/*
 * -----------------------------------------------------
 * Simple mocking of classes
 * -----------------------------------------------------
 */

class Type1 {
public:

   virtual bool bar() {
      return true;
   }

   virtual int foo(int n) {

      if (!bar())
         return -1;

      return n * 10;
   }

   virtual ~Type1() = default;
};

class MockType1 : public Type1 {
public:

   MOCK_METHOD(bool, bar, (), (override));
   MOCK_METHOD(int, foo, (int), (override));
};

TEST(experiment, ex1)
{
   NiceMock<MockType1> mock;

   EXPECT_CALL(mock, bar()).WillRepeatedly([&mock] () {
      return mock.Type1::bar();
   });

   ON_CALL(mock, foo).WillByDefault([&mock](int n) {
      return mock.Type1::foo(n);
   });

   ASSERT_EQ(mock.foo(5), 50);
   ASSERT_EQ(mock.foo(6), 60);
}

TEST(experiment, ex2)
{
   NiceMock<MockType1> mock;

   EXPECT_CALL(mock, bar()).WillOnce(Return(false));

   ON_CALL(mock, foo).WillByDefault([&mock](int n) {
      return mock.Type1::foo(n);
   });

   ASSERT_EQ(mock.foo(5), -1);
}
