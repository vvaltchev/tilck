/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/boot.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/process.h>

void (*hw_read_clock)(struct datetime *) = &hw_read_clock_cmos;
ulong uefi_rt_addr;

void uefi_set_rt_pointer(ulong addr)
{
   ASSERT(!uefi_rt_addr);
   uefi_rt_addr = addr;
}

#if defined(__i386__) || defined(__x86_64__)

#define EFI_DEBUG 0
#include <efi.h>
#include <efiapi.h>

void hw_read_clock_uefi(struct datetime *out)
{
   EFI_RUNTIME_SERVICES *RT = PA_TO_LIN_VA(uefi_rt_addr);
   EFI_STATUS status;
   EFI_TIME t;

   status = RT->GetTime(&t, NULL);

   if (status != EFI_SUCCESS)
      panic("UEFI: GetTime() failed with: %lu", status);

   *out = (struct datetime) {
      .sec = t.Second,
      .min = t.Minute,
      .hour = t.Hour,
      .day = t.Day,
      .month = t.Month,
      .year = t.Year
   };
}

void setup_uefi_runtime_services(void)
{
   struct mem_region ma;
   EFI_STATUS status;
   EFI_RUNTIME_SERVICES *RT;
   EFI_MEMORY_DESCRIPTOR *virt_map;
   EFI_TIME t;
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
      const ulong pend = MIN(
         (ulong)(ma.addr + ma.len),
         (ulong)LINEAR_MAPPING_SIZE
      );
      const size_t page_count = (pend - pbegin) >> PAGE_SHIFT;
      const bool rw = (ma.type == TILCK_BOOT_EFI_RUNTIME_RW);

      const size_t count =
         map_pages(get_kernel_pdir(),
                   TO_PTR(pbegin),
                   pbegin,
                   page_count,
                   (rw ? PAGING_FL_RW : 0));

      if (count != page_count)
         panic("Unable to map regions in the virtual space");
   }

   if (!(virt_map = kmalloc(num_entries * sizeof(EFI_MEMORY_DESCRIPTOR))))
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
      desc.VirtualStart = (ulong)PA_TO_LIN_VA(ma.addr);
      desc.NumberOfPages = ma.len >> PAGE_SHIFT;
      desc.Attribute = EFI_MEMORY_RUNTIME;

      virt_map[num_entries++] = desc;
   }

   status =
      RT->SetVirtualAddressMap(num_entries * sizeof(EFI_MEMORY_DESCRIPTOR),
                               sizeof(EFI_MEMORY_DESCRIPTOR),
                               EFI_MEMORY_DESCRIPTOR_VERSION,
                               virt_map);

   if (status != EFI_SUCCESS)
      panic("Failed to set the UEFI virtual map");

   RT = PA_TO_LIN_VA(RT);

   /* Pollute the virtual map object, to make sure it's not used anymore */
   memset(virt_map, 0xAA, num_entries * sizeof(EFI_MEMORY_DESCRIPTOR));

   /* Free the virtual map object */
   kfree2(virt_map, num_entries * sizeof(EFI_MEMORY_DESCRIPTOR));

   for (int i = 0; i < get_mem_regions_count(); i++) {

      get_mem_region(i, &ma);

      if (ma.type != TILCK_BOOT_EFI_RUNTIME_RO &&
          ma.type != TILCK_BOOT_EFI_RUNTIME_RW)
      {
         continue;
      }

      const ulong pbegin = (ulong)ma.addr;
      const ulong pend = MIN(
         (ulong)(ma.addr + ma.len),
         (ulong)LINEAR_MAPPING_SIZE
      );
      const size_t page_count = (pend - pbegin) >> PAGE_SHIFT;
      unmap_pages(get_kernel_pdir(),
                  TO_PTR(pbegin),
                  page_count,
                  false);
   }

   /*
    * Make sure that UEFI Runtime Services
    * actually work by calling GetTime() and
    * fail early if they don't.
    */
   status = RT->GetTime(&t, NULL);

   if (status != EFI_SUCCESS)
      panic("Failed to setup UEFI runtime services");

   hw_read_clock = &hw_read_clock_uefi;
}

#else

void setup_uefi_runtime_services(void)
{
   /* do nothing */
}

#endif
