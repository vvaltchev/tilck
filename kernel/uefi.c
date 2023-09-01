/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/boot.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/process.h>

#define EFI_DEBUG 0
#include <efi.h>
#include <efiapi.h>

void (*hw_read_clock)(struct datetime *out) = &hw_read_clock_cmos;
static ulong uefi_rt_addr;

void uefi_set_rt_pointer(ulong addr)
{
   ASSERT(!uefi_rt_addr);
   uefi_rt_addr = addr;
}

void hw_read_clock_uefi(struct datetime *out)
{
   struct datetime d;
   EFI_STATUS status;
   EFI_TIME Time;
   EFI_RUNTIME_SERVICES *RT = TO_PTR(KERNEL_PA_TO_VA(uefi_rt_addr));

   status = RT->GetTime(&Time, NULL);

   if (status != EFI_SUCCESS)
      panic("UEFI: Failed to get time");

   d.sec = Time.Second;
   d.min = Time.Minute;
   d.hour = Time.Hour;
   d.day = Time.Day;
   d.month = Time.Month;
   d.year = Time.Year;

   *out = d;
}

void setup_uefi_runtime_services(void)
{
   struct mem_region ma;
   EFI_STATUS status;
   EFI_RUNTIME_SERVICES *RT;
   EFI_MEMORY_DESCRIPTOR *virt_map;
   unsigned int num_entries = 0;

   if (!uefi_rt_addr)
      return;

   RT = TO_PTR(uefi_rt_addr);

   for (int i = 0; i < get_mem_regions_count(); i++) {

      get_mem_region(i, &ma);

      if (ma.type == TILCK_BOOT_EFI_RUNTIME_RO ||
          ma.type == TILCK_BOOT_EFI_RUNTIME_RW)
      {
         num_entries++;
      }
   }

   /*
    * When SetVirtualAddressMap() is called, the physical
    * addresses should be directly accessible. So, identity
    * map them.
    */
   for (int i = 0; i < get_mem_regions_count(); i++) {

      get_mem_region(i, &ma);

      if (ma.type != TILCK_BOOT_EFI_RUNTIME_RO &&
          ma.type != TILCK_BOOT_EFI_RUNTIME_RW)
      {
         continue;
      }

      const ulong pbegin = (ulong)ma.addr;
      const ulong pend = MIN((ulong)(ma.addr+ma.len), (ulong)LINEAR_MAPPING_SIZE);
      const size_t page_count = (pend - pbegin) >> PAGE_SHIFT;
      const bool rw = (ma.type == TILCK_BOOT_EFI_RUNTIME_RW);

      ulong *vbegin = TO_PTR(pbegin);

      size_t count =
         map_pages(get_kernel_pdir(),
                   vbegin,
                   pbegin,
                   page_count,
                   (rw ? PAGING_FL_RW : 0));

      if (count != page_count)
         panic("Unable to map regions in the virtual space");
   }

   virt_map = kmalloc((size_t)num_entries);

   if (!virt_map)
      panic("Failed to allocate the UEFI virtual map");

   num_entries = 0;

   for (int i = 0; i < get_mem_regions_count(); i++) {

      EFI_MEMORY_DESCRIPTOR desc;
      get_mem_region(i, &ma);

      if (ma.type != TILCK_BOOT_EFI_RUNTIME_RO &&
          ma.type != TILCK_BOOT_EFI_RUNTIME_RW)
      {
         continue;
      }

      /*
       * Consider Write-Protected (WP) (or Read-Only (RO),
       * UEFI Spec 2.5+) regions as Runtime Code regions
       * and the rest as Runtime Data regions.
       */
      desc.Type =
         ma.type == TILCK_BOOT_EFI_RUNTIME_RO
            ? EfiRuntimeServicesCode
            : EfiRuntimeServicesData;

      desc.PhysicalStart = (ulong)ma.addr;
      desc.VirtualStart = (ulong)KERNEL_PA_TO_VA(ma.addr);
      desc.NumberOfPages = ma.len >> PAGE_SHIFT;
      desc.Attribute = EFI_MEMORY_RUNTIME;

      virt_map[num_entries++] = desc;
   }

   status = RT->SetVirtualAddressMap(num_entries * sizeof(EFI_MEMORY_DESCRIPTOR),
                                     sizeof(EFI_MEMORY_DESCRIPTOR),
                                     EFI_MEMORY_DESCRIPTOR_VERSION,
                                     virt_map);

   if (status != EFI_SUCCESS)
      panic("Failed to set the UEFI virtual map");

   RT = KERNEL_PA_TO_VA(RT);

   memset(virt_map, 0, (size_t)num_entries * sizeof(EFI_MEMORY_DESCRIPTOR));
   kfree(virt_map);

   for (int i = 0; i < get_mem_regions_count(); i++) {

      get_mem_region(i, &ma);

      if (ma.type != TILCK_BOOT_EFI_RUNTIME_RO &&
          ma.type != TILCK_BOOT_EFI_RUNTIME_RW)
      {
         continue;
      }

      const ulong pbegin = (ulong)ma.addr;
      const ulong pend = MIN((ulong)(ma.addr+ma.len), (ulong)LINEAR_MAPPING_SIZE);
      const size_t page_count = (pend - pbegin) >> PAGE_SHIFT;

      ulong *vbegin = TO_PTR(pbegin);

      unmap_pages(get_kernel_pdir(),
                  vbegin,
                  page_count,
                  false);
   }

   /*
    * Make sure that UEFI Runtime Services
    * actually work by calling GetTime() and
    * fail early if they don't.
    */
   EFI_TIME Time;
   status = RT->GetTime(&Time, NULL);

   if (status != EFI_SUCCESS)
      panic("Failed to setup UEFI runtime services");

   hw_read_clock = &hw_read_clock_uefi;
}
