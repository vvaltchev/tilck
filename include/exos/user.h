
#pragma once
#include <common/basic_defs.h>

extern volatile bool in_user_copy;

static ALWAYS_INLINE bool is_in_user_copy(void)
{
   return in_user_copy;
}

void handle_user_copy_fault(void);
int copy_from_user(void *dest, const void *user_ptr, size_t n);
int copy_str_from_user(void *dest, const void *user_ptr);
int copy_to_user(void *user_ptr, const void *src, size_t n);
