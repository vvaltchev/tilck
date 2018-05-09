
#include <common/string_util.h>
#include <exos/hal.h>

#define CMOS_CONTROL_PORT 0x70
#define CMOS_DATA_PORT 0x71

#define REG_SECONDS 0x00
#define REG_MINUTES 0x02
#define REG_HOURS 0x04
#define REG_WEEKDAY 0x06
#define REG_DAY 0x07
#define REG_MONTH 0x08
#define REG_YEAR 0x09

#define REG_STATUS_REG_A 0x0A
#define REG_STATUS_REG_B 0x0B

typedef struct {

   union {

      struct {
         u8 sec;
         u8 min;
         u8 hour;
         u8 weekday;
         u8 day;
         u8 month;
         u16 year;
      };

      u64 raw;
   };

} datetime_t;

static inline u32 bcd_to_dec(u32 bcd)
{
   return ((bcd & 0xF0) >> 1) + ((bcd & 0xF0) >> 3) + (bcd & 0xf);
}

static inline bool cmos_is_update_in_progress(void)
{
   outb(CMOS_CONTROL_PORT, REG_STATUS_REG_A);
   return inb(CMOS_DATA_PORT) & 0x80;
}

static void cmod_read_datetime_raw(datetime_t *d)
{
   int NMI_disable_bit = 0; // temporary

   outb(CMOS_CONTROL_PORT, (NMI_disable_bit << 7) | REG_SECONDS);
   d->sec = inb(CMOS_DATA_PORT);

   outb(CMOS_CONTROL_PORT, (NMI_disable_bit << 7) | REG_MINUTES);
   d->min = inb(CMOS_DATA_PORT);

   outb(CMOS_CONTROL_PORT, (NMI_disable_bit << 7) | REG_HOURS);
   d->hour = inb(CMOS_DATA_PORT);

   outb(CMOS_CONTROL_PORT, (NMI_disable_bit << 7) | REG_DAY);
   d->day = inb(CMOS_DATA_PORT);

   outb(CMOS_CONTROL_PORT, (NMI_disable_bit << 7) | REG_MONTH);
   d->month = inb(CMOS_DATA_PORT);

   outb(CMOS_CONTROL_PORT, (NMI_disable_bit << 7) | REG_YEAR);
   d->year = inb(CMOS_DATA_PORT);
}

void cmos_read_datetime(void)
{
   int NMI_disable_bit = 0; // temporary

   datetime_t d, dlast;
   int reg_b;
   bool use_24h;
   bool use_binary;
   bool hour_pm_bit;


   outb(CMOS_CONTROL_PORT, (NMI_disable_bit << 7) | REG_STATUS_REG_B);
   reg_b = inb(CMOS_DATA_PORT);
   use_24h = !!(reg_b & (1 << 1));
   use_binary = !!(reg_b & (1 << 2));

   while (cmos_is_update_in_progress()); // wait an eventual update to complete
   cmod_read_datetime_raw(&d);

   do {

      dlast = d;
      while (cmos_is_update_in_progress()); // wait an eventual update to complete
      cmod_read_datetime_raw(&d);

      /*
       * read until we get the same result twice: this is necessary to get a
       * consistent set of values.
       */

   } while (dlast.raw != d.raw);

   hour_pm_bit = d.hour & 0x80;
   d.hour &= ~0x80;

   if (!use_binary) {
      d.sec = bcd_to_dec(d.sec);
      d.min = bcd_to_dec(d.min);
      d.hour = bcd_to_dec(d.hour);
      d.day = bcd_to_dec(d.day);
      d.month = bcd_to_dec(d.month);
      d.year = bcd_to_dec(d.year);
   }

   if (!use_24h) {
      if (d.hour == 12) {
         if (!hour_pm_bit)
            d.hour = 0; /* 12 am is midnight => hour 0 */
      } else {
         if (hour_pm_bit)
            d.hour = (d.hour + 12) % 24;
      }
   }

   d.year += 2000;

   printk("date & time [24h: %i, bin: %i]: %i/%i/%i %i:%i:%i\n",
          use_24h, use_binary, d.day, d.month, d.year, d.hour, d.min, d.sec);
}
