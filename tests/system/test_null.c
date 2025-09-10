#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "devshell.h"

void cmd_dev_null(int argc, char **argv)
{
   int valid, rc, fd;
   char buf[32] = { [0 ... 31] = 0x41 };

   fd = open("/dev/null", O_RDWR);
   DEVSHELL_CMD_ASSERT(fd > 0);

   rc = read(fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == 0);
   rc = lseek(fd, 0, SEEK_CUR);
   DEVSHELL_CMD_ASSERT(rc == 0);

   valid = 0;
   for (int i = 0; i < sizeof(buf); ++i)
      valid += buf[i] == 0x41 ? 1 : 0;
   DEVSHELL_CMD_ASSERT(valid == sizeof(buf));

   rc = write(fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == sizeof(buf));
   rc = lseek(fd, 0, SEEK_CUR);
   DEVSHELL_CMD_ASSERT(rc == 0);

   close(fd);
}

void cmd_dev_zero(int argc, char **argv)
{
   int valid, rc, fd;
   char buf[32] = { [0 ... 31] = 0x41 };

   fd = open("/dev/zero", O_RDWR);
   DEVSHELL_CMD_ASSERT(fd > 0);

   rc = read(fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == sizeof(buf));
   rc = lseek(fd, 0, SEEK_CUR);
   DEVSHELL_CMD_ASSERT(rc == 0);

   valid = 0;
   for (int i = 0; i < sizeof(buf); ++i)
      valid += buf[i] == 0x00 ? 1 : 0;
   DEVSHELL_CMD_ASSERT(valid == sizeof(buf));

   rc = write(fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == sizeof(buf));

   rc = lseek(fd, 0, SEEK_CUR);
   DEVSHELL_CMD_ASSERT(rc == 0);

   close(fd);
}

void cmd_dev_full(int argc, char **argv)
{
   int valid, rc, fd;
   char buf[32] = { [0 ... 31] = 0x41 };

   fd = open("/dev/full", O_RDWR);
   DEVSHELL_CMD_ASSERT(fd > 0);

   rc = read(fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc == sizeof(buf));
   rc = lseek(fd, 0, SEEK_CUR);
   DEVSHELL_CMD_ASSERT(rc == 0);

   valid = 0;
   for (int i = 0; i < sizeof(buf); ++i)
      valid += buf[i] == 0x00 ? 1 : 0;
   DEVSHELL_CMD_ASSERT(valid == sizeof(buf));

   errno = 0;
   rc = write(fd, buf, sizeof(buf));
   DEVSHELL_CMD_ASSERT(rc < 0);
   DEVSHELL_CMD_ASSERT(errno == ENOSPC);

   rc = lseek(fd, 0, SEEK_CUR);
   DEVSHELL_CMD_ASSERT(rc == 0);

   close(fd);
}
