#include <common_defs.h>

#ifndef UNIT_TEST_ENVIRONMENT

NORETURN void assert_failed(const char *expr, const char *file, int line)
{
   panic("ASSERTION '%s' FAILED in file '%s' at line %i\n",
         expr, file, line);
}

NORETURN void not_reached(const char *file, int line)
{
   panic("NOT_REACHED in file '%s' at line %i\n", file, line);
}

#endif
