phys = 0x00100000;

SECTIONS
{
  .text phys : AT(phys) {
    code = .;
    *(.text)
    *(.rodata)
  }

  .data : AT(phys + (data - code))
  {
    data = .;
    *(.data)
  }

}