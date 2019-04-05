/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _KMALLOC_C_

   #error This is NOT a header file and it is not meant to be included

   /*
    * The only purpose of this file is to keep kmalloc.c shorter.
    * Yes, this file could be turned into a regular C source file, but at the
    * price of making several static functions and variables in kmalloc.c to be
    * just non-static. We don't want that. Code isolation is a GOOD thing.
    */

#endif

typedef struct {

   void *vaddr;
   size_t size;
   void *caller_eip;
   bool leaked;

} alloc_entry;

static bool leak_detector_enabled;

#if KMALLOC_SUPPORT_LEAK_DETECTOR

static u32 alloc_entries_count;
static alloc_entry alloc_entries[1024];
static void *metadata_copies[KMALLOC_HEAPS_COUNT];

void debug_kmalloc_start_leak_detector(bool save_metadata)
{
   disable_preemption();

   bzero(alloc_entries, sizeof(alloc_entries));
   alloc_entries_count = 0;

   if (save_metadata) {

      for (u32 i = 0; i < ARRAY_SIZE(metadata_copies); i++) {

         if (!heaps[i].metadata_size)
            continue;

         void *buf = kmalloc(heaps[i].metadata_size);

         if (!buf)
            panic("leak detector: unable to alloc buffer for metadata copy");

         metadata_copies[i] = buf;
      }

      for (u32 i = 0; i < ARRAY_SIZE(metadata_copies); i++) {

         if (!heaps[i].metadata_size)
            continue;

         memcpy(metadata_copies[i],
                heaps[i].metadata_nodes,
                heaps[i].metadata_size);
      }
   }

   leak_detector_enabled = true;

   enable_preemption();
}

void debug_kmalloc_stop_leak_detector(bool show_leaks)
{
   disable_preemption();
   leak_detector_enabled = false;

   if (!show_leaks)
      goto end;

   u32 leak_count = 0;

   for (u32 i = 0; i < alloc_entries_count; i++) {

      if (!alloc_entries[i].leaked)
         continue;

      printk("Leaked block at %p (%u B), caller eip: %p\n",
             alloc_entries[i].vaddr,
             alloc_entries[i].size,
             alloc_entries[i].caller_eip);

      leak_count++;
   }

   printk("Total allocs: %u\n", alloc_entries_count);
   printk("Leak count: %u\n", leak_count);

   for (u32 i = 0; i < ARRAY_SIZE(metadata_copies); i++) {

      block_node *md_copy = metadata_copies[i];

      if (!md_copy)
         continue;

      block_node *md = heaps[i].metadata_nodes;
      size_t len = heaps[i].metadata_size;

      for (u32 j = 0; j < len; j++) {
         if (md[j].raw == md_copy[j].raw)
            continue;

         printk("Heap[%d] metadata node #%u DIFFERS [va: %p]\n", i, j, &md[j]);
         printk("saved: { split: %d, full: %d }\n",
                md_copy[j].split, md_copy[j].full);
         printk("curr:  { split: %d, full: %d }\n", md[j].split, md[j].full);
      }
   }

end:

   for (u32 i = 0; i < ARRAY_SIZE(metadata_copies); i++) {
      if (metadata_copies[i]) {
         kfree2(metadata_copies[i], heaps[i].metadata_size);
         metadata_copies[i] = NULL;
      }
   }

   enable_preemption();
}

static NO_INLINE void debug_kmalloc_register_alloc(void *vaddr, size_t s)
{
   disable_preemption();
   VERIFY(alloc_entries_count < ARRAY_SIZE(alloc_entries) - 1);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-address"

   void *eip_raw = __builtin_return_address(1);
   void *eip = __builtin_extract_return_addr(eip_raw);

#pragma GCC diagnostic pop

   alloc_entries[alloc_entries_count++] = (alloc_entry) {
      .vaddr = vaddr,
      .size = s,
      .caller_eip = eip,
      .leaked = true,
   };

   enable_preemption();
}

static NO_INLINE void debug_kmalloc_register_free(void *vaddr, size_t s)
{
   disable_preemption();

   for (u32 i = 0; i < alloc_entries_count; i++) {
      if (alloc_entries[i].vaddr == vaddr) {
         VERIFY(alloc_entries[i].size == s);
         alloc_entries[i].leaked = false;
         enable_preemption();
         return;
      }
   }

   panic("free block at %p, allocated (probably) "
         "before the start of the leak detector\n", vaddr);

   enable_preemption(); // in case 'panic' is replaced with a warning.
}

#else

#define debug_kmalloc_register_alloc(...)
#define debug_kmalloc_register_free(...)

#endif
