
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>

#include <multiboot.h>

extern u32 memsize_in_mb;

static char mem_areas_buf[512 * sizeof(multiboot_memory_map_t)];
multiboot_memory_map_t *const mem_areas = (void *)mem_areas_buf;
uptr mem_areas_tot_size;

void save_multiboot_memory_map(multiboot_info_t *mbi)
{
   mem_areas_tot_size = MIN(sizeof(mem_areas_buf), mbi->mmap_length);

   memcpy(mem_areas,
          (void *)(uptr)(mbi->mmap_addr),
          mem_areas_tot_size);
}

const char *mem_region_type_to_str(u32 type)
{
   static const char *str_types[] = {
      "",
      "regular",
      "reserved",
      "acpi_reclaimable",
      "acpi_nvs_mem",
      "bad_memory"
   };

   if (type > ARRAY_SIZE(str_types))
      return "unknown";

   return str_types[type];
}

void dump_system_memory_map(void)
{
   multiboot_memory_map_t *ma = (void *)mem_areas;

   while ( ((uptr)ma-(uptr)mem_areas) < mem_areas_tot_size ) {

      printk("0x%llx - 0x%llx (%d)\n",
             ma->addr, ma->len, ma->type);
      ma = (void *)ma + ma->size + 4;
   }
}
