#include <cstdio>
#include <gtest/gtest.h>

using namespace std;
using namespace testing;

int main(int argc, char **argv)
{
   InitGoogleTest(&argc, argv);
   return RUN_ALL_TESTS();
}
