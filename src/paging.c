
#include <paging.h>
#include <irq.h>
#include <stringUtil.h>

void handle_page_fault(struct regs *r)
{
   printk("Page fault handling..\n");
}

void init_paging()
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
}
