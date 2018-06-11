
#include <common/basic_defs.h>

#include <exos/process.h>
#include <exos/kmalloc.h>
#include <exos/errno.h>

void user_vfree_and_unmap(uptr user_vaddr, int page_count)
{
   for (int i = 0; i < page_count; i++) {
      uptr va = user_vaddr + i * PAGE_SIZE;
      uptr pa = get_mapping(get_curr_page_dir(), (void *)va);
      kfree2(KERNEL_PA_TO_VA(pa), PAGE_SIZE);
      unmap_page(get_curr_page_dir(), (void *)va);
   }
}

bool user_valloc_and_map(uptr user_vaddr, int page_count)
{
   for (int i = 0; i < page_count; i++) {

      void *kernel_vaddr = kmalloc(PAGE_SIZE);

      if (!kernel_vaddr) {
         user_vfree_and_unmap(user_vaddr, i);
         return false;
      }

      uptr pa = KERNEL_VA_TO_PA(kernel_vaddr);
      uptr va = user_vaddr + i * PAGE_SIZE;
      map_page(get_curr_page_dir(), (void *)va, pa, true, true);
   }

   return true;
}


sptr sys_brk(void *new_brk)
{
   task_info *ti = get_curr_task();
   process_info *pi = ti->pi;

   if (!new_brk)
      goto ret;

   // TODO: check if Linux accepts non-page aligned addresses.
   // If yes, what to do? how to approx? truncation, round-up/round-down?
   if ((uptr)new_brk & OFFSET_IN_PAGE_MASK)
      goto ret;

   if (new_brk < pi->initial_brk)
      goto ret;

   if ((uptr)new_brk >= MAX_BRK)
      goto ret;

   if (new_brk == pi->brk)
      goto ret;

   disable_preemption();

   /*
    * Disable preemption to avoid any threads to mess-up with the address space
    * of the current process (i.e. they might call brk(), mmap() etc.)
    */

   if (new_brk < pi->brk) {

      /* we have to free pages */
      void *vaddr = pi->brk;

      while (vaddr > new_brk) {
         const uptr paddr = get_mapping(pi->pdir, vaddr - PAGE_SIZE);
         kfree2(KERNEL_PA_TO_VA(paddr), PAGE_SIZE);
         unmap_page(pi->pdir, vaddr - PAGE_SIZE);
         vaddr -= PAGE_SIZE;
      }

      pi->brk = vaddr;
      goto out;
   }

   void *vaddr = pi->brk;

   while (vaddr < new_brk) {

      if (is_mapped(pi->pdir, vaddr))
         goto out; // error: vaddr is already mapped!

      vaddr += PAGE_SIZE;
   }

   /* OK, everything looks good here */

   vaddr = pi->brk;

   while (vaddr < new_brk) {

      void *kernel_vaddr = kmalloc(PAGE_SIZE);

      if (!kernel_vaddr)
         break; /* we've allocated as much as possible */

      const uptr paddr = KERNEL_VA_TO_PA(kernel_vaddr);
      map_page(pi->pdir, vaddr, paddr, true, true);
      vaddr += PAGE_SIZE;
   }

   /* We're done. */
   pi->brk = vaddr;

out:
   enable_preemption();
ret:
   return (sptr)pi->brk;
}

#define PROT_NONE       0x0             /* Page can not be accessed.  */
#define PROT_READ       0x1             /* Page can be read.  */
#define PROT_WRITE      0x2             /* Page can be written.  */
#define PROT_EXEC       0x4             /* Page can be executed.  */

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20

sptr
sys_mmap_pgoff(void *addr, size_t len, int prot,
               int flags, int fd, off_t pgoffset)
{
   printk("mmap2(addr: %p, len: %u, prot: %u, flags: %p, fd: %d, off: %d)\n",
          addr, len, prot, flags, fd, pgoffset);

   if (addr != NULL)
      return -EINVAL;

   if (flags != (MAP_ANONYMOUS | MAP_PRIVATE))
      return -EINVAL;

   if (fd != -1 || pgoffset != 0)
      return -EINVAL;

   if (!(prot & PROT_READ))
      return -EINVAL;

   return -ENOSYS;
}

sptr sys_munmap(void *vaddr, size_t len)
{
   return -ENOSYS;
}
