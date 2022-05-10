Contributing to Tilck
---------------------------

First off, thanks for taking the time to contribute!

Any kind of contribution to the Tilck project is more than welcome. The project
needs features and tests in many areas in order to become production-ready. Pull
requests will be reviewed and, after addressing any eventual comments from the
maintainer, will be merged in the master branch. Pull requests should meet a few
requirements:

  - Contain high-quality code following Tilck's coding style
  - Build with *all* the currently supported configurations (see [building])
  - Pass *all* the existing tests (see [testing])
  - Contain tests for the new code
  - Contain expressive commit messages
  - Implement features aligned with the goals of the Tilck project

**NOTE:** don't be scared to propose an imperfect pull request. You will get help!
 
Code-free contributions are also welcome. The project would benefit from more
documentation (HOWTOs, tutorials etc.), but also from artistic assets.

[building]: docs/building.md
[testing]: docs/testing.md

Coding style
-------------------------

The project does not have (yet) a formal coding style document, but it's possible
to quickly mention a few points:

  - The snake_case is used
  - The line length is *strictly* limited to 80 columns
  - The indentation is *3 spaces* (see https://github.com/vvaltchev/tilck/discussions/88)
  - The opening braces are generally on the same line for all code blocks except
    for functions bodies and the initialization of arrays:
      ```C
      void func(int x)
      {
         if (x == 3) {
            do_something();
            do_something_else();
         }
      }

      static const char *const default_env[] =
      {
         "OSTYPE=linux-gnu",
         "TERM=linux",
         /* ... */
      };
      ```
  - An exception to the rule above is for complex `if` statements that need to be broken
    across multiple lines:
    ```C
    if (eh->header->e_ident[EI_CLASS] != ELF_CURR_CLASS ||
        eh->header->e_machine != ELF_CURR_ARCH)
    {
       *wrong_arch = true;
       return -ENOEXEC;
    }
    ```
  - The braces should be omitted for single-statement blocks unless that generates confusion
  - It is often better to leave an empty line after `if (...) {` or `for (...) {`, when the
    condition/loop header is long compared to the next line, the body is non-trivial and the
    open brace is on the same line as the `if`/`for` statement.
    For example, this is good:
    ```C
     for (int i = (int)h->heap_data_size_log2 - 1; i >= 0 && tot < size; i--) {

        const size_t sub_block_size = (1 << i);

        if (!(size & sub_block_size))
           continue;
        
        /* ... */
     }
    ```
    While this looks kind of ugly:
    ```C
     for (int i = (int)h->heap_data_size_log2 - 1; i >= 0 && tot < size; i--) {
        const size_t sub_block_size = (1 << i);

        if (!(size & sub_block_size))
           continue;
        
        /* ... */
     }
     ```
     Simply because in the second case, the first line of the body gets partially hidden
     by loop's header.
    
  - For long function signatures, the type goes on the previous line, and the parameters
    are aligned like this:
      ```C
      static int
      load_segment_by_mmap(fs_handle *elf_h,
                           pdir_t *pdir,
                           Elf_Phdr *phdr,
                           ulong *end_vaddr_ref)
      ```
  - A similar rule applies for long function calls:
      ```C
      rc = map_page(pdir,
                    (void *)stack_top + (i << PAGE_SHIFT),
                    KERNEL_VA_TO_PA(p),
                    PAGING_FL_RW | PAGING_FL_US);
      ```
  - However, it's not always required to have only a single argument per line:
      ```C
      written += snprintk(buf + written, sizeof(buf) - (u32)written,
                          "%i ", nested_interrupts[i]);
      ```
  - Assignment to all the fields of a struct is generally done with this syntax:
      ```C
      *ctx = (struct deferred_kfree_ctx) {
         .h = h,
         .ptr = ptr,
         .user_size = *size,
         .flags = flags
      };
      ```
  - When it's possible to do so, it's preferable to put the code under a lock
    in a nested code block:
      ```C
      ulong var;
      disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */
      {
         nested_interrupts_count--;
         ASSERT(nested_interrupts_count >= 0);
      }
      enable_interrupts(&var);
      ```
  - Comments are great. Long comments are formatted like this:
      ```C
      /*
       * This is a long comment.
       * Second line.
       * Third line.
       */
      ```
  - Null-pointer checks are generally performed without NULL:
    ```C
    if (ptr)
      do_something();
      
    if (!ptr_b)
      return;
    ```
  - Long expressions involving the ternary operator `?:` can be formatted like this:
      ```C
      new_term =
         serial_port_fwd
            ? tty_allocate_and_init_new_serial_term(serial_port_fwd)
            : tty_allocate_and_init_new_video_term(rows_buf);      
      ```
 
 **NOTE[1]:** all the rules above can be broken if there is enough benefit for doing so.
 The only rule that has no exceptions so far is the 80-column limit for lines. Keeping
 that rule improves the readability of the code and prevents the use of excessively long
 names for identifiers.
 
 **NOTE[2]:** project's coding style is *not* written in stone. Proposals to change it
 will be carefully considered, as long as the person advocating for the change is
 willing to invest some effort in converting the pre-existing code to the new style.
