
#include <tilck/kernel/syscalls.h>

/* Actual implementation, not a stub: only the root user exists. */
sptr sys_getuid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root group exists. */
sptr sys_getgid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root user exists. */
sptr sys_geteuid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root group exists. */
sptr sys_getegid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root user exists. */
sptr sys_setuid(uptr uid)
{
   if (uid == 0)
      return 0;

   return -EINVAL;
}

/* Actual implementation, not a stub: only the root group exists. */
sptr sys_setgid(uptr gid)
{
   if (gid == 0)
      return 0;

   return -EINVAL;
}

/* Actual implementation: accept only 0 as UID. */
sptr sys_setuid16(uptr uid)
{
   return sys_setuid((u16)uid);
}

/* Actual implementation, not a stub: only the root user exists. */
sptr sys_getuid16()
{
   return 0;
}

sptr sys_setgid16(uptr gid)
{
   return sys_setgid((u16)gid);
}

/* Actual implementation, not a stub: only the root group exists. */
sptr sys_getgid16()
{
   return 0;
}

/* Actual implementation, not a stub: only the root user exists. */
sptr sys_geteuid16()
{
   return 0;
}

/* Actual implementation, not a stub: only the root group exists. */
sptr sys_getegid16()
{
   return 0;
}
