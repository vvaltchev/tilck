/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/list.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

ACPI_MODULE_NAME("osl_malloc")

#define ACPI_HEAP_SIZE                 (128 * KB)
#define ACPI_HEAP_MBS                          32
#define ACPI_HEAP_MAX_OBJ_SIZE                128

static struct kmalloc_heap *acpi_heap;
static ulong acpi_heap_va;
static ulong acpi_heap_last_obj_addr;

static void *
acpi_osl_do_alloc(size_t sz)
{
   void *vaddr = NULL;

   if (sz <= ACPI_HEAP_MAX_OBJ_SIZE) {

      disable_preemption();
      {
         vaddr = per_heap_kmalloc(acpi_heap, &sz, 0);
      }
      enable_preemption_nosched();
   }

   if (!vaddr)
      vaddr = kmalloc(sz);

   return vaddr;
}

static void
acpi_osl_do_free(void *ptr)
{
   ulong va = (ulong)ptr;
   size_t sz = 0;

   if (IN_RANGE_INC(va, acpi_heap_va, acpi_heap_last_obj_addr)) {

      disable_preemption();
      {
         per_heap_kfree(acpi_heap, ptr, &sz, 0);
      }
      enable_preemption_nosched();

   } else {
      kfree(ptr);
   }
}

void *
AcpiOsAllocate(ACPI_SIZE Size)
{
   const size_t sz = (size_t)Size;
   void *vaddr;
   ulong var;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (Size >= 512 * MB) {

      /*
       * Don't allow that, ever. Note some kind of check is mandatory, even if
       * the machine has more than 512 MB of contiguous free memory, just
       * because ACPI_SIZE is 64-bit wide, while kmalloc()'s size parameter
       * is a pointer-size integer. In general, ACPICA shouldn't even consider
       * allocating so large chunks of memory, no matter what. ACPI_SIZE is
       * 64-bit wide just because it's used for other purposes as well (memory
       * regions, even on 32-bit systems, can indeed be larger than 2^32 bytes).
       */
      return_PTR(NULL);
   }

   // if (in_irq()) {
   //    printk("ACPI: AcpiOsAllocate(%zu) called in IRQ context\n", sz);
   // }

   disable_interrupts(&var);
   {
      vaddr = acpi_osl_do_alloc(sz);
   }
   enable_interrupts(&var);
   return_PTR(vaddr);
}

void
AcpiOsFree(void *Memory)
{
   ACPI_FUNCTION_TRACE(__FUNC__);

   ulong var;
   disable_interrupts(&var);
   {
      acpi_osl_do_free(Memory);
   }
   enable_interrupts(&var);
   return_VOID;
}

ACPI_STATUS
osl_init_malloc(void)
{
   acpi_heap_va = (ulong)kmalloc(ACPI_HEAP_SIZE);

   if (!acpi_heap_va)
      return AE_NO_MEMORY;

   acpi_heap = kmalloc_create_regular_heap(acpi_heap_va,
                                           ACPI_HEAP_SIZE,
                                           ACPI_HEAP_MBS);

   if (!acpi_heap) {
      kfree2((void *)acpi_heap_va, ACPI_HEAP_SIZE);
      acpi_heap_va = 0;
      return AE_NO_MEMORY;
   }

   acpi_heap_last_obj_addr = acpi_heap_va + ACPI_HEAP_SIZE - ACPI_HEAP_MBS;
   return AE_OK;
}
