

/*
 * exOS's toy virtual file system
 *
 * As this project's goals are by far different from the Linux ones, this
 * layer won't provide anything even as close as what Linux's VFS does.
 * Its purpose is to provide the MINIMUM NECESSARY to allow basic operations
 * like open, read, write, close to work both on FAT32 and on character devices
 * like /dev/tty0 (when it will implemented).
 *
 */

typedef void *fs_file_handle;


typedef fs_file_handle (*func_open) (const char *);
typedef void (*func_close) (fs_file_handle);



typedef struct {

   char name[16];



} filesystem;

