# SPDX-License-Identifier: BSD-2-Clause

import gdb # pylint: disable=import-error

type_task = gdb.lookup_type("struct task")
type_task_p = type_task.pointer()

type_process = gdb.lookup_type("struct process")
type_process_p = type_process.pointer()

list_node = gdb.lookup_type("struct list_node")
list_node_p = list_node.pointer()

fs_handle_base = gdb.lookup_type("struct fs_handle_base")
fs_handle_base_p = fs_handle_base.pointer()

type_user_mapping = gdb.lookup_type("struct user_mapping")
type_user_mapping_p = type_user_mapping.pointer()

multi_obj_waiter = gdb.lookup_type("struct multi_obj_waiter")
multi_obj_waiter_p = multi_obj_waiter.pointer()
