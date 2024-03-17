# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(
   WRAPPED_SYMS

   assert_failed
   not_reached
   not_implemented
   tilck_vprintk
   kmutex_lock
   kmutex_unlock
   fat_ramdisk_prepare_for_mmap
   wth_create_thread_for
   wth_wakeup
   check_in_irq_handler
   general_kmalloc
   general_kfree
   kmalloc_get_first_heap
   vfs_dup
   vfs_close
   use_kernel_arg
   handle_sys_trace_arg
   copy_str_from_user
   copy_from_user

   experiment_bar
   experiment_foo
)
