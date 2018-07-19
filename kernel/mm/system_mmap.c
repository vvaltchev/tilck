
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/utils.h>

#include <exos/kernel/paging.h>
#include <exos/kernel/sort.h>

#include <multiboot.h>
#include <elf.h>

#define MEM_REG_EXTRA_RAMDISK  1
#define MEM_REG_EXTRA_KERNEL   2

typedef struct {

   u64 addr;
   u64 len;
   u32 type;  /* multiboot_memory_map_t's type */
   u32 extra; /* bit mask */

} memory_region_t;

u32 memsize_in_mb;

memory_region_t mem_areas[512];
uptr mem_areas_count;

uptr ramdisk_paddr;
size_t ramdisk_size;

static int less_than_cmp_mem_area(const void *a, const void *b)
{
   const memory_region_t *m1 = a;
   const memory_region_t *m2 = b;

   if (m1->addr < m2->addr)
      return -1;

   if (m1->addr == m2->addr)
      return 0;

   return 1;
}

void align_mem_areas_to_page_boundary(void)
{
   for (u32 i = 0; i < mem_areas_count; i++) {

      memory_region_t *ma = mem_areas + i;

      /*
       * Unfortunately, in general we cannot rely on the memory regions to be
       * page-aligned (while they will be in most of the cases). Therefore,
       * we have to forcibly approximate the regions at page-boundaries.
       */
      const u64 ma_end = round_up_at64(ma->addr + ma->len, PAGE_SIZE);
      ma->addr &= PAGE_MASK;
      ma->len = ma_end - ma->addr;
   }
}

void merge_adj_mem_regions(void)
{
   for (int i = 0; i < (int)mem_areas_count - 1; i++) {

      memory_region_t *ma = mem_areas + i;
      memory_region_t *ma_next = ma + 1;

      if (ma_next->type != ma->type || ma_next->extra != ma->extra)
         continue;

      if (ma_next->addr != ma->addr + ma->len)
         continue;

      /* If we got here, we hit two adjacent regions having the same type */

      const int rem = mem_areas_count - i - 2;

      ma->len += ma_next->len;
      memcpy(ma_next, ma_next + 1, rem * sizeof(memory_region_t));
      mem_areas_count--; /* decrease the number of memory regions */
      i--; /* compensate the i++ in the for loop: we have to keep the index */
   }
}

void fix_mem_areas(void)
{
   align_mem_areas_to_page_boundary();

   insertion_sort_generic(mem_areas,
                          sizeof(memory_region_t),
                          mem_areas_count,
                          less_than_cmp_mem_area);

   merge_adj_mem_regions();
}

static void add_kernel_phdrs_to_mmap(void)
{
#ifdef BITS32

   Elf32_Ehdr *h = (Elf32_Ehdr*)(KERNEL_PA_TO_VA(KERNEL_PADDR));
   Elf32_Phdr *phdrs = (void *)h + h->e_phoff;

   for (int i = 0; i < h->e_phnum; i++) {

      Elf32_Phdr *phdr = phdrs + i;

      if (phdr->p_type != PT_LOAD)
         continue;

      mem_areas[mem_areas_count++] = (memory_region_t) {
         .addr = phdr->p_paddr,
         .len = phdr->p_memsz,
         .type = MULTIBOOT_MEMORY_RESERVED,
         .extra = MEM_REG_EXTRA_KERNEL
      };
   }

#else
   NOT_IMPLEMENTED();
#endif
}

void save_multiboot_memory_map(multiboot_info_t *mbi)
{
   u32 size = MIN(sizeof(mem_areas), mbi->mmap_length);
   uptr ma_addr = mbi->mmap_addr;

   while (ma_addr < mbi->mmap_addr + size) {

      multiboot_memory_map_t *ma = (void *)ma_addr;
      mem_areas[mem_areas_count++] = (memory_region_t) {
         .addr = ma->addr,
         .len = ma->len,
         .type = ma->type,
         .extra = 0
      };
      ma_addr += ma->size + 4;
   }

   if (ramdisk_size) {
      mem_areas[mem_areas_count++] = (memory_region_t) {
         .addr = ramdisk_paddr,
         .len = ramdisk_size,
         .type = MULTIBOOT_MEMORY_RESERVED,
         .extra = MEM_REG_EXTRA_RAMDISK
      };
   }

   add_kernel_phdrs_to_mmap();
   fix_mem_areas();
}

static const char *mem_region_extra_to_str(u32 e)
{
   if (e == MEM_REG_EXTRA_RAMDISK)
      return "RDSK";

   if (e == MEM_REG_EXTRA_KERNEL)
      return "KRNL";

   return "    ";
}

void dump_system_memory_map(void)
{
   printk("System's memory map\n");
   printk("---------------------------------------------------------------\n");
   printk("       START                 END        (T, Extr)\n");
   for (u32 i = 0; i < mem_areas_count; i++) {

      memory_region_t *ma = mem_areas + i;

      printk("0x%llx - 0x%llx (%d, %s) [%u KB]\n",
             ma->addr, ma->addr + ma->len,
             ma->type, mem_region_extra_to_str(ma->extra), ma->len / KB);
   }
   printk("---------------------------------------------------------------\n");
}
