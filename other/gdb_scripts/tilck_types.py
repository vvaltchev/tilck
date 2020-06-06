# SPDX-License-Identifier: BSD-2-Clause

import gdb # pylint: disable=import-error

task = gdb.lookup_type("struct task")
task_p = task.pointer()

process = gdb.lookup_type("struct process")
process_p = process.pointer()

list_node = gdb.lookup_type("struct list_node")
list_node_p = list_node.pointer()

fs_handle_base = gdb.lookup_type("struct fs_handle_base")
fs_handle_base_p = fs_handle_base.pointer()

user_mapping = gdb.lookup_type("struct user_mapping")
user_mapping_p = user_mapping.pointer()

multi_obj_waiter = gdb.lookup_type("struct multi_obj_waiter")
multi_obj_waiter_p = multi_obj_waiter.pointer()
