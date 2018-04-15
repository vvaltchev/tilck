
#pragma once
#include <common/basic_defs.h>

bool is_in_user_copy(void);
void handle_user_copy_fault(void);

int copy_from_user(void *dest, const void *user_ptr, size_t n);
int copy_str_from_user(void *dest, const void *user_ptr);
int copy_to_user(void *user_ptr, const void *src, size_t n);
int check_user_ptr_size_writable(void *user_ptr);
int check_user_ptr_size_readable(void *user_ptr);
