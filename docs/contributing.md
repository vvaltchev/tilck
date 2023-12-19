Contributing to Tilck
---------------------------

First off, thanks for taking the time to contribute!

Contributions to the Tilck project are more than welcome. The project needs
features and tests in many areas in order to become production-ready. Pull
requests will be reviewed and, after addressing any eventual comments from the
maintainer, will be merged in the master branch. Pull requests should meet a few
requirements:

  - Contain high-quality code following Tilck's coding style
  - Build with *all* the currently supported configurations (see [building])
  - Pass *all* the existing tests (see [testing])
  - Contain tests for the new code
  - Contain expressive commit messages
  - Implement features aligned with the goals of the Tilck project

[building]: building.md
[testing]: testing.md

Required knowledge
-------------------------

The required knowledge depends a lot on the type of contributions you're
interested in making. Unit tests require a fair amount of C++ experience.
The system tests (shellcmds) require experience with C and system programming.
Working on the test runners require some experience with Python. Changes to
the `build_toolchain` script and related require good Bash skills and
deep knowledge about how to build C/C++ projects with cross-compilers. Tilck's
build itself requires CMake experience.

Contributions to the kernel Tilck are the generally harder compared to the
rest of the components and require a *significant* experience with C, *good*
theoretical understanding of operating systems and at least *some* practical
experience with embedded or OS development or at least good knowledge of x86
assembly. More or less knowledge is required to start contributing in the kernel,
depending on the task. For beginners in the field, refer to:

- https://wiki.osdev.org/Introduction
- https://wiki.osdev.org/Required_Knowledge

Commit style & pull requests
------------------------------

Each **commit** must be self-contained: no ugly temporary hacks between commits, no commits
that do not compile individually, no commits that do not all the pre-existing pass tests.
That is *critical* in order to `git bisect` to be useable. If a commit does not compile
or breaks tests because it is incomplete somehow, it will be much harder to reason about
whether that specific commit introduced a subtle regression or not. Otherwise, with
`git bisect` is easy to narrow down a subtle regression to a single commit.

However, a commit might contain only dead code that is activated in follow-up changes:
as long as everything builds, that's totally fine. Also, an *accurate* 1-line summary
is mandatory for each commit, using at most 74 characters, 80 as absolute max. Ideally,
after leaving an empty line, there should be also a detailed description on the reasons
behind the change and implementation details, depending on the complexity of the change.
However, at least at the moment, having anything beyond a good 1-line summary is optional,
except for rare cases like subtle bug fixes.

A **pull request** is either made by one *single* polished commit that is edited
during the review iterations and updated via `git push --force` on the topic branch
until the pull request is merged (**model 1**) OR by a curated *series* of micro-commits
in the Linux kernel style that is edited *as series* on each review iteration (**model 2**).

Using model 2 means making the logical steps as small as possible and always separate
changes in several layers in different commits. Each mechanical operation (e.g. rename)
has it's own commit with summary etc. Each dependency is addressed as a separate commit,
for example: if the new feature X requires a code change in the pre-existing subsystem Y,
the improvement in Y happens first in a dedicated commit and then X is implemented.
Almost every commit is a *preparation* for the following one, while still being completely
*self-contained* as in model 1. The granularity is as fine as possible, as long as the change
can be self-contained and have a logical consistency. For example, typically it makes no sense
to write the code of a new function in multiple steps, but it might make sense to introduce
a "skeleton change" with empty functions, in certain cases. If changes to pre-existing
functions can be split in smaller still-working steps that are logically separated, using
dedicated commits would be the way to go.

**Model 2** is *superior* compared to model 1 because it makes the reviews and the later debugging
or bisect easier: that's why it's used by the **Linux kernel community**. However, it imposes a
*very significant* overhead on the contributors, not only because it requires more work for
each pull request to be prepared in the first place, but because it requires significantly much
more work on each review iteration. Indeed, with model 2, on each review iteration, it will be
necessary to modify one or more commits, therefore re-writing the history of the topic-branch with
the git interactive rebase feature (`rebase -i`) and resolving all the rebase conflicts that will
be generated because of that. Also, commits might be re-ordered, squashed or split. Finally, because
that is error-prone even for expert developers, on each iteration, it is necessary to rebuild Tilck
in *all* the configurations, for *every* single commit in the series, to make sure that the series
doesn't break anything, at no point in time.

Because of model 2's overhead and complexity, the preferred model in Tilck is **model 1**. We can
afford that since Tilck is a medium-sized project and doesn't have many contributors. However,
contributors exicited to learn the Linux-kernel contribution style, who want to use Tilck as a
form of *preparation* for contributing to a major open source project like Linux, might use
model 2, accepting the extra overhead for that.

**TL;DR;** Unless you want to learn the Linux kernel contribution style and have plenty of time for
that, use **model 1** and always squash your temporary commits before preparing the pull-request.

Coding style
-------------------------

The project does not have (yet) a formal coding style document, but it's possible
to quickly mention a few points:

  - The snake_case is used
  - The line length is *strictly* limited to 80 columns
  - The indentation is *3 spaces* ([see here](https://github.com/vvaltchev/tilck/discussions/88))
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
