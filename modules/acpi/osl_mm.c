/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/paging.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/acpiosxf.h>
#include <3rd_party/acpi/acexcep.h>

void *
AcpiOsMapMemory(
    ACPI_PHYSICAL_ADDRESS   Where,
    ACPI_SIZE               Length)
{
   ACPI_PHYSICAL_ADDRESS paddr = Where & PAGE_MASK;
   void *va;
   size_t pg_count;
   size_t cnt;

   if (Where + Length <= LINEAR_MAPPING_SIZE)
      return KERNEL_PA_TO_VA(Where);

   if (!(va = hi_vmem_reserve(Length)))
      return NULL;

   Length = Where + Length - paddr;
   pg_count = pow2_round_up_at(Length, PAGE_SIZE) >> PAGE_SHIFT;
   cnt = map_pages(get_kernel_pdir(), va, paddr, pg_count, PAGING_FL_RW);

   if (cnt < pg_count) {
      unmap_pages_permissive(get_kernel_pdir(), va, cnt, false);
      hi_vmem_release(va, Length);
      return NULL;
   }

   printk("ACPI: mmap %zu pages %p -> %p\n", pg_count, TO_PTR(Where), va);
   return va;
}

void
AcpiOsUnmapMemory(
    void                    *LogicalAddr,
    ACPI_SIZE               Size)
{
   ulong vaddr = (ulong)LogicalAddr;
   ulong aligned_vaddr = vaddr & PAGE_MASK;
   size_t pg_count;

   if (vaddr + Size <= LINEAR_MAPPING_END)
      return;

   Size = vaddr + Size - aligned_vaddr;
   pg_count = pow2_round_up_at(Size, PAGE_SIZE) >> PAGE_SHIFT;
   printk("ACPI: release %zu pages mapped at %p\n", pg_count, LogicalAddr);
   unmap_pages(get_kernel_pdir(), TO_PTR(aligned_vaddr), pg_count, false);
   hi_vmem_release(LogicalAddr, Size);
}
