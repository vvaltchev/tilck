/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/syscalls.h>

/* Actual implementation, not a stub: only the root user exists. */
int sys_getuid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root group exists. */
int sys_getgid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root user exists. */
int sys_geteuid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root group exists. */
int sys_getegid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root user exists. */
int sys_setuid(ulong uid)
{
   if (uid == 0)
      return 0;

   return -EINVAL;
}

/* Actual implementation, not a stub: only the root group exists. */
int sys_setgid(ulong gid)
{
   if (gid == 0)
      return 0;

   return -EINVAL;
}

/* Actual implementation: accept only 0 as UID. */
int sys_setuid16(ulong uid)
{
   return sys_setuid((u16)uid);
}

/* Actual implementation, not a stub: only the root user exists. */
int sys_getuid16()
{
   return 0;
}

int sys_setgid16(ulong gid)
{
   return sys_setgid((u16)gid);
}

/* Actual implementation, not a stub: only the root group exists. */
int sys_getgid16()
{
   return 0;
}

/* Actual implementation, not a stub: only the root user exists. */
int sys_geteuid16()
{
   return 0;
}

/* Actual implementation, not a stub: only the root group exists. */
int sys_getegid16()
{
   return 0;
}
