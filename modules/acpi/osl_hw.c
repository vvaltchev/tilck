/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/mods/pci.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

ACPI_MODULE_NAME("osl_hw")

STATIC_ASSERT(IRQ_HANDLED == ACPI_INTERRUPT_HANDLED);
STATIC_ASSERT(IRQ_NOT_HANDLED == ACPI_INTERRUPT_NOT_HANDLED);

static struct irq_handler_node *osl_irq_handlers;

ACPI_STATUS
osl_init_irqs(void)
{
   osl_irq_handlers = kzalloc_array_obj(struct irq_handler_node, 16);

   if (!osl_irq_handlers)
      panic("ACPI: unable to allocate memory for IRQ handlers");

   return AE_OK;
}

ACPI_STATUS
AcpiOsInstallInterruptHandler(
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine,
    void                    *Context)
{
   struct irq_handler_node *n;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (!ServiceRoutine)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   if (!IN_RANGE((int)InterruptNumber, 0, 16))
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   n = &osl_irq_handlers[InterruptNumber];

   if (n->handler)
      return_ACPI_STATUS(AE_ALREADY_EXISTS);

   list_node_init(&n->node);
   n->handler = (irq_handler_t)ServiceRoutine;
   n->context = Context;

   printk("ACPI: install handler for IRQ #%u\n", InterruptNumber);
   irq_install_handler(InterruptNumber, n);
   return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler(
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine)
{
   struct irq_handler_node *n;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (!ServiceRoutine)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   if (!IN_RANGE((int)InterruptNumber, 0, 16))
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   n = &osl_irq_handlers[InterruptNumber];

   if (!n->handler)
      return_ACPI_STATUS(AE_NOT_EXIST);

   if (n->handler != ServiceRoutine)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   printk("ACPI: remove handler for IRQ #%u\n", InterruptNumber);
   irq_uninstall_handler(InterruptNumber, n);
   return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
AcpiOsReadPciConfiguration(
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  *Value,
    UINT32                  Width)
{
   u32 val;
   int rc;

   ACPI_FUNCTION_TRACE(__FUNC__);

   if (Width == 64)
      return_ACPI_STATUS(AE_SUPPORT);

   rc = pci_config_read(
      pci_make_loc(
         PciId->Segment,
         PciId->Bus,
         PciId->Device,
         PciId->Function
      ),
      Reg,
      Width,
      &val
   );

   switch (rc) {

      case 0:
         break; /* everything is fine */

      case -ERANGE: /* fall-through */
      case -EINVAL:
         return_ACPI_STATUS(AE_BAD_PARAMETER);

      default:
         return_ACPI_STATUS(AE_ERROR);
   }

   *Value = val;
   return AE_OK;
}

ACPI_STATUS
AcpiOsWritePciConfiguration(
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  Value,
    UINT32                  Width)
{
   int rc;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (Width == 64)
      return_ACPI_STATUS(AE_SUPPORT);

   rc = pci_config_write(
      pci_make_loc(
         PciId->Segment,
         PciId->Bus,
         PciId->Device,
         PciId->Function
      ),
      Reg,
      Width,
      (u32)Value
   );

   switch (rc) {

      case 0:
         break; /* everything is fine */

      case -ERANGE: /* fall-through */
      case -EINVAL:
         return_ACPI_STATUS(AE_BAD_PARAMETER);

      default:
         return_ACPI_STATUS(AE_ERROR);
   }

   return AE_OK;
}
