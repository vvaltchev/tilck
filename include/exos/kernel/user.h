
#pragma once
#include <exos/common/basic_defs.h>

static inline bool user_out_of_range(const void *user_ptr, size_t n)
{
   return ((uptr)user_ptr + n) > KERNEL_BASE_VA;
}

int copy_from_user(void *dest, const void *user_ptr, size_t n);
int copy_to_user(void *user_ptr, const void *src, size_t n);
int check_user_ptr_size_writable(void *user_ptr);

int copy_str_from_user(void *dest,
                       const void *user_ptr,
                       size_t max_size,
                       size_t *written_ptr);

int copy_str_array_from_user(void *dest,
                             const char *const *argv,
                             size_t max_size,
                             size_t *written_ptr);

int duplicate_user_path(char *dest,
                        const char *user_path,
                        size_t dest_size,
                        size_t *written_ptr /* IN/OUT */);

int duplicate_user_argv(char *dest,
                        const char *const *user_argv,
                        size_t dest_size,
                        size_t *written_ptr /* IN/OUT */);
