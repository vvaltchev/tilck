
#include "defs.h"
#include "utils.h"

#define EFI_MBI_MAX_ADDR (64 * KB)

EFI_MEMORY_DESCRIPTOR mmap[512];

multiboot_info_t *mbi;
multiboot_memory_map_t *multiboot_mmap;
UINT32 mmap_elems_count;

EFI_STATUS
AllocateMbi(void)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_PHYSICAL_ADDRESS multiboot_buffer = EFI_MBI_MAX_ADDR;

   status = BS->AllocatePages(AllocateMaxAddress,
                              EfiLoaderData,
                              1,
                              &multiboot_buffer);
   HANDLE_EFI_ERROR("AllocatePages");

   BS->SetMem((void *)(UINTN)multiboot_buffer, 1 * PAGE_SIZE, 0);
   mbi = (multiboot_info_t *)(UINTN)multiboot_buffer;

end:
   return status;
}

EFI_STATUS
MbiSetFramebufferInfo(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info,
                      UINTN fb_addr)
{
   EFI_STATUS status = EFI_SUCCESS;

   mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
   mbi->framebuffer_addr = fb_addr;
   mbi->framebuffer_pitch =
      mode_info->PixelsPerScanLine * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
   mbi->framebuffer_width = mode_info->HorizontalResolution,
   mbi->framebuffer_height = mode_info->VerticalResolution,
   mbi->framebuffer_bpp = sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * 8;
   mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;

   if (mode_info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
      mbi->framebuffer_red_field_position = 16;
      mbi->framebuffer_green_field_position = 8;
      mbi->framebuffer_blue_field_position = 0;
   } else if (mode_info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
      mbi->framebuffer_red_field_position = 0;
      mbi->framebuffer_green_field_position = 8;
      mbi->framebuffer_blue_field_position = 16;
   }

   mbi->framebuffer_red_mask_size = 8;
   mbi->framebuffer_green_mask_size = 8;
   mbi->framebuffer_blue_mask_size = 8;

end:
   return status;
}

static UINT32 EfiToMultibootMemType(UINT32 type)
{
   switch (type) {

      case EfiReservedMemoryType:
      case EfiRuntimeServicesCode:
      case EfiRuntimeServicesData:
         return MULTIBOOT_MEMORY_RESERVED;

      case EfiLoaderCode:
      case EfiLoaderData:
      case EfiBootServicesCode:
      case EfiBootServicesData:
      case EfiConventionalMemory:
         return MULTIBOOT_MEMORY_AVAILABLE;

      case EfiUnusableMemory:
         return MULTIBOOT_MEMORY_BADRAM;

      case EfiACPIReclaimMemory:
         return MULTIBOOT_MEMORY_ACPI_RECLAIMABLE;

      case EfiACPIMemoryNVS:
         return MULTIBOOT_MEMORY_NVS;

      case EfiMemoryMappedIO:
      case EfiMemoryMappedIOPortSpace:
      case EfiPalCode:
         return MULTIBOOT_MEMORY_RESERVED;
   }
}

static void AddMemoryRegion(UINT64 start, UINT64 end, UINT32 type)
{
   mbi->flags |= MULTIBOOT_INFO_MEMORY;

   if (type == MULTIBOOT_MEMORY_AVAILABLE) {
      if (start < mbi->mem_lower * KB)
         mbi->mem_lower = start / KB;

      if (end > mbi->mem_upper * KB)
         mbi->mem_upper = end / KB;
   }

   multiboot_mmap[mmap_elems_count++] = (multiboot_memory_map_t) {
      .size = sizeof(multiboot_memory_map_t) - sizeof(u32),
      .addr = (multiboot_uint64_t)start,
      .len = (multiboot_uint64_t)(end - start),
      .type = type
   };
}

EFI_STATUS
MultibootSaveMemoryMap(UINTN *mapkey)
{
   EFI_MEMORY_DESCRIPTOR *desc = NULL;
   EFI_STATUS status = EFI_SUCCESS;
   UINT32 last_type = (UINT32) -1;
   UINT64 last_start = 0;
   UINT64 last_end = 0;
   UINTN mmap_size;
   UINTN desc_size;
   UINT32 desc_ver;

   EFI_PHYSICAL_ADDRESS multiboot_mmap_paddr = EFI_MBI_MAX_ADDR;

   status = BS->AllocatePages(AllocateMaxAddress,
                              EfiLoaderData,
                              1,
                              &multiboot_mmap_paddr);
   HANDLE_EFI_ERROR("AllocatePages");

   BS->SetMem((void *)(UINTN)multiboot_mmap_paddr, 1 * PAGE_SIZE, 0);
   multiboot_mmap = (multiboot_memory_map_t *)(UINTN)multiboot_mmap_paddr;

   mmap_size = sizeof(mmap);
   status = BS->GetMemoryMap(&mmap_size, mmap, mapkey, &desc_size, &desc_ver);
   HANDLE_EFI_ERROR("BS->GetMemoryMap");

   mbi->flags |= MULTIBOOT_INFO_MEM_MAP;
   desc = (void *)mmap;

   do {

      UINT32 type = EfiToMultibootMemType(desc->Type);
      UINT64 start = desc->PhysicalStart;
      UINT64 end = start + desc->NumberOfPages * 4096;

      if (last_type != type || last_end != start) {

         /*
          * The new region is not contiguous with the previous one OR it has
          * a different type.
          */

         if (last_type != (UINT32)-1) {
            AddMemoryRegion(last_start, last_end, last_type);
         }

         last_type = type;
         last_start = start;
      }

      /*
       * last_type == type && last_end == start
       *
       * We're continuing a region of the same "multiboot type", just move the
       * end forward.
       */
      last_end = end;

      desc = (void *)desc + desc_size;

   } while ((UINTN)desc < (UINTN)mmap + mmap_size);

   AddMemoryRegion(last_start, last_end, last_type);

   mbi->mmap_addr = (UINTN)multiboot_mmap;
   mbi->mmap_length = mmap_elems_count * sizeof(multiboot_memory_map_t);

end:
   return status;
}

EFI_STATUS
MbiSetRamdisk(EFI_PHYSICAL_ADDRESS ramdisk_paddr, UINTN ramdisk_size)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_PHYSICAL_ADDRESS multiboot_mod_addr = EFI_MBI_MAX_ADDR;
   multiboot_module_t *mod;

   status = BS->AllocatePages(AllocateMaxAddress,
                              EfiLoaderData,
                              1,
                              &multiboot_mod_addr);
   HANDLE_EFI_ERROR("AllocatePages");

   BS->SetMem((void *)(UINTN)multiboot_mod_addr, 1 * PAGE_SIZE, 0);

   mod = (multiboot_module_t *)(UINTN)multiboot_mod_addr;
   mod->mod_start = ramdisk_paddr;
   mod->mod_end = mod->mod_start + ramdisk_size;

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (UINTN)mod;
   mbi->mods_count = 1;

end:
   return status;
}
