/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/debug_utils.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

ACPI_MODULE_NAME("osl_mm")

void *
AcpiOsMapMemory(
    ACPI_PHYSICAL_ADDRESS   Where,
    ACPI_SIZE               RawLength)
{
   ACPI_PHYSICAL_ADDRESS paddr = Where & PAGE_MASK;
   ACPI_SIZE Length = pow2_round_up_at(Where + RawLength - paddr, PAGE_SIZE);
   size_t cnt, pg_count;
   void *va;

   ACPI_FUNCTION_TRACE(__FUNC__);

   if (paddr + Length <= LINEAR_MAPPING_SIZE)
      return_PTR(PA_TO_LIN_VA(Where));

   // printk("ACPI: mmap 0x%08llx (len: %zu -> %zuK)\n",
   //        paddr, RawLength, Length/KB);

   if (!(va = hi_vmem_reserve(Length))) {
      ACPI_ERROR((AE_INFO, "hi_vmem_reserve() failed\n"));
      return_PTR(NULL);
   }

   pg_count = Length >> PAGE_SHIFT;
   cnt = map_kernel_pages(va, paddr, pg_count, PAGING_FL_RW);

   if (cnt < pg_count) {
      unmap_pages_permissive(get_kernel_pdir(), va, cnt, false);
      hi_vmem_release(va, Length);
      ACPI_ERROR((AE_INFO, "cnt (%zu) < pg_count (%zu)\n", cnt, pg_count));
      return_PTR(NULL);
   }

   return_PTR(TO_PTR((ulong)va + (Where & OFFSET_IN_PAGE_MASK)));
}

void
AcpiOsUnmapMemory(
    void                    *LogicalAddr,
    ACPI_SIZE               RawSz)
{
   ulong vaddr = (ulong)LogicalAddr;
   ulong aligned_vaddr = vaddr & PAGE_MASK;
   ACPI_SIZE Size = pow2_round_up_at(vaddr + RawSz - aligned_vaddr, PAGE_SIZE);
   size_t pg_count;

   ACPI_FUNCTION_TRACE(__FUNC__);

   if (aligned_vaddr + Size <= LINEAR_MAPPING_END)
      return_VOID;

   //printk("ACPI: UNmap %p (len: %zu -> %zuK)\n",
   //       LogicalAddr, RawSz, Size/KB);

   pg_count = Size >> PAGE_SHIFT;
   unmap_kernel_pages(TO_PTR(aligned_vaddr), pg_count, false);
   hi_vmem_release(TO_PTR(aligned_vaddr), Size);
   return_VOID;
}

ACPI_STATUS
AcpiOsGetPhysicalAddress(
    void                    *LogicalAddress,
    ACPI_PHYSICAL_ADDRESS   *PhysicalAddress)
{
   ulong paddr;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (!LogicalAddress || !PhysicalAddress)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   if (get_mapping2(get_kernel_pdir(), LogicalAddress, &paddr) < 0)
      return_ACPI_STATUS(AE_ERROR);

   *PhysicalAddress = paddr;
   return_ACPI_STATUS(AE_OK);
}

BOOLEAN
AcpiOsReadable(
    void                    *Pointer,
    ACPI_SIZE               Length)
{
   ulong va = (ulong)Pointer;
   ulong va_end = va + Length;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (va < BASE_VA)
      return_UINT8(false);

   if (va_end <= LINEAR_MAPPING_END)
      return_UINT8(true);

   while (va < va_end) {

      if (!is_mapped(get_kernel_pdir(), TO_PTR(va)))
         return_UINT8(false);

      va += PAGE_SIZE;
   }

   return_UINT8(true);
}

BOOLEAN
AcpiOsWritable(
    void                    *Pointer,
    ACPI_SIZE               Length)
{
   ulong va = (ulong)Pointer;
   ulong va_end = va + Length;
   struct mem_region m;
   int reg_count = get_mem_regions_count();
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (va < BASE_VA)
      return_UINT8(false);

   for (int i = 0; i < reg_count; i++) {

      get_mem_region(i, &m);

      if (m.type != MULTIBOOT_MEMORY_AVAILABLE)
         continue;

      if (~m.extra & MEM_REG_EXTRA_KERNEL)
         continue;

      if (~m.extra & MEM_REG_EXTRA_RAMDISK)
         continue;

      /* OK, now `m` points to a kernel/ramdisk region */
      if (IN_RANGE(va, m.addr, m.addr + m.len)) {

         /*
          * The address falls inside a read/write protected region.
          * We cannot allow ACPICA to believe it's writable.
          */

         return_UINT8(false);
      }
   }

   if (va_end <= LINEAR_MAPPING_END)
      return_UINT8(true);

   while (va < va_end) {

      if (!is_rw_mapped(get_kernel_pdir(), TO_PTR(va)))
         return_UINT8(false);

      va += PAGE_SIZE;
   }

   return_UINT8(true);
}

ACPI_STATUS
AcpiOsReadMemory(
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  *Value,
    UINT32                  Width)
{
   void *va;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if ((Address + (Width >> 3)) > LINEAR_MAPPING_SIZE) {

      /*
       * In order to support this, we'll need to implement some sort of
       * memory mapping cache. Mapping and un-mapping a page for a single
       * read/write is definitively unaccetable.
       */

      NOT_IMPLEMENTED();

   } else {
      va = PA_TO_LIN_VA(Address);
   }

   switch (Width) {
      case 8:
         *Value = *(volatile u8 *)va;
         break;
      case 16:
         *Value = *(volatile u16 *)va;
         break;
      case 32:
         *Value = *(volatile u32 *)va;
         break;
      case 64:
         *Value = *(volatile u64 *)va;
         break;
      default:
         return_ACPI_STATUS(AE_BAD_PARAMETER);
   }

   return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
AcpiOsWriteMemory(
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  Value,
    UINT32                  Width)
{
   void *va;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if ((Address + (Width >> 3)) > LINEAR_MAPPING_SIZE) {

      /* See the comment in AcpiOsReadMemory() */
      NOT_IMPLEMENTED();

   } else {
      va = PA_TO_LIN_VA(Address);
   }

   switch (Width) {
      case 8:
         *(volatile u8 *)va = Value;
         break;
      case 16:
         *(volatile u16 *)va = Value;
         break;
      case 32:
         *(volatile u32 *)va = Value;
         break;
      case 64:
         *(volatile u64 *)va = Value;
         break;
      default:
         return_ACPI_STATUS(AE_BAD_PARAMETER);
   }

   return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
AcpiOsReadPort(
    ACPI_IO_ADDRESS         Address,
    UINT32                  *Value,
    UINT32                  Width)
{
   u16 ioport = (u16)Address;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (Address > 0xffff)
      return_ACPI_STATUS(AE_NOT_EXIST);

   switch (Width) {
      case 8:
         *Value = (u32)inb(ioport);
         break;
      case 16:
         *Value = (u32)inw(ioport);
         break;
      case 32:
         *Value = (u32)inl(ioport);
         break;
      default:
         return_ACPI_STATUS(AE_BAD_PARAMETER);
   }

   return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
AcpiOsWritePort(
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
   u16 ioport = (u16)Address;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (Address > 0xffff)
      return_ACPI_STATUS(AE_NOT_EXIST);

   switch (Width) {
      case 8:
         outb(ioport, (u8)Value);
         break;
      case 16:
         outw(ioport, (u16)Value);
         break;
      case 32:
         outl(ioport, (u32)Value);
         break;
      default:
         return_ACPI_STATUS(AE_BAD_PARAMETER);
   }

   return_ACPI_STATUS(AE_OK);
}
