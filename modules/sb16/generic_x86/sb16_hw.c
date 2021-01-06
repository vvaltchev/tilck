/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/tilck_sound.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/arch/generic_x86/dma.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/hal.h>

#include "sb16.h"

u8 sb16_curr_pause_cmd;
u8 sb16_curr_cont_cmd;

u16 sb16_curr_ack_cmd;
u16 sb16_curr_mute_sound;

static u16 sb16_curr_rate;

u8
sb16_get_irq(void)
{
   outb(DSP_MIXER, 0x80);
   u8 irq_code = inb(DSP_MIXER_DATA);
   u8 irq;

   switch (irq_code) {

      case 0x01:
         irq = 2;
         break;

      case 0x04:
         irq = 7;
         break;

      case 0x08:
         irq = 10;
         break;

      case 0x02: /* fall-through */
      default:
         irq = 5;
         break;
   }

   return irq;
}

int
sb16_detect_dsp_hw_and_reset(void)
{
   u8 status;

   if (!in_hypervisor()) {

      /*
       * It makes sense trying to support such ancient ISA hardware ONLY inside
       * VMs. In particular, only QEMU has a limited support for SoundBlaster16.
       */
      return -ENODEV;
   }

   outb(DSP_RESET, 1);
   delay_us(3);
   outb(DSP_RESET, 0);
   delay_us(1);

   /*
    * On real HW we would poll on the status register, waiting for bit 7 to
    * become set for about 100 us before giving up. But, in case of virtualized
    * hardware, that makes no sense: IN/OUT instructions trigger interrupts in
    * the hypervisor and are handled immediately.
    */
   status = inb(DSP_READ_STATUS);

   if (~status & 128)
      return -ENODEV;

   delay_us(1);

   /*
    * As above, we should poll on DSP_READ for ~100 us on real hardware, but
    * that's not our case.
    */
   status = inb(DSP_READ);

   if (status != 0xAA)
      return -ENODEV;

   return 0;
}

int
sb16_check_version(void)
{
   outb(DSP_WRITE, DSP_GET_VER);
   sb16_info.ver_major = inb(DSP_READ);
   sb16_info.ver_minor = inb(DSP_READ);

   if (sb16_info.ver_major < 4) {
      printk("sb16: DSP (ver. %u.%u) too old\n",
             sb16_info.ver_major, sb16_info.ver_minor);
      return -1;
   }

   return 0;
}

void
sb16_program_dma(u8 bits, u32 buf_sz)
{
   u32 cnt;
   u16 mask_reg_cmd;
   u16 rst_ff_cmd;
   u16 mode_reg;
   u16 ch_page_reg;
   u16 ch_start_reg;
   u16 ch_count_reg;
   u16 offset;
   u8 channel;
   u8 dma_mode;

   ASSERT(bits == 8 || bits == 16);

   if (bits == 8) {
      cnt = buf_sz;
      mask_reg_cmd = DMA8_SINGLE_CH_MASK_REG;
      rst_ff_cmd = DMA8_RST_FLIP_FLOP;
      mode_reg = DMA8_MODE_REG;
      ch_page_reg = DMA_CHANNEL_1_PAGE_REG;
      ch_start_reg = DMA_CHANNEL_1_START_REG;
      ch_count_reg = DMA_CHANNEL_1_COUNT_REG;
      offset = sb16_info.buf_paddr & 0xffff;
      channel = DMA_CHANNEL_1;
   } else {
      cnt = buf_sz / 2;
      mask_reg_cmd = DMA16_SINGLE_CH_MASK_REG;
      rst_ff_cmd = DMA16_RST_FLIP_FLOP;
      mode_reg = DMA16_MODE_REG;
      ch_page_reg = DMA_CHANNEL_5_PAGE_REG;
      ch_start_reg = DMA_CHANNEL_5_START_REG;
      ch_count_reg = DMA_CHANNEL_5_COUNT_REG;
      offset = (sb16_info.buf_paddr & 0xffff) >> 1;
      channel = DMA_CHANNEL_5;
   }

   dma_mode = DMA_SINGLE_MODE | DMA_READ_TX | channel;

   if (buf_sz == 64 * KB) {
      SB16_DBG("prog DMA in AUTO INIT mode\n");
      dma_mode |= DMA_AUTO_INIT;
   } else {
      SB16_DBG("prog DMA in SINGLE CYCLE mode\n");
   }

   outb(mask_reg_cmd, DMA_MASK_CHANNEL | channel);
   outb(rst_ff_cmd, 1);
   outb(mode_reg, dma_mode);

   outb(ch_page_reg,  (sb16_info.buf_paddr   >> 16) & 0xff);
   outb(ch_start_reg, (offset      ) & 0xff); // low byte
   outb(ch_start_reg, (offset >>  8) & 0xff); // high byte

   outb(ch_count_reg, ((cnt-1)     ) & 0xff); // low byte
   outb(ch_count_reg, ((cnt-1) >> 8) & 0xff); // high byte
   outb(mask_reg_cmd, channel);
}

void
sb16_set_sample_rate(u16 sample_rate)
{
   if (sample_rate != sb16_curr_rate) {
      outb(DSP_WRITE, DSP_SET_SAMPLE_R);
      outb(DSP_WRITE, (sample_rate >> 8) & 0xff); // high
      outb(DSP_WRITE, (sample_rate     ) & 0xff); // low
      sb16_curr_rate = sample_rate;
   }
}

void
sb16_program(struct tilck_sound_params *p, u32 buf_sz)
{
   u8 prog_mode = 0;
   u8 sound_fmt = 0;
   u32 samples_cnt;

   prog_mode |= DSP_PLAY;

   if (buf_sz == 32 * KB) {
      SB16_DBG("prog DSP in AUTO_INIT mode\n");
      prog_mode |= DSP_AUTO_INIT;
   } else {
      SB16_DBG("prog DSP in SINGLE CYCLE mode\n");
   }

   if (p->bits == 8) {

      samples_cnt = buf_sz;
      prog_mode |= DSP_8_BIT_PROG;

      sb16_curr_pause_cmd = DSP_8_BIT_PAUSE;
      sb16_curr_cont_cmd = DSP_8_BIT_CONT;
      sb16_curr_ack_cmd = DSP_8_BIT_ACK;

   } else {

      samples_cnt = buf_sz >> 1;
      prog_mode |= DSP_16_BIT_PROG;

      sb16_curr_pause_cmd = DSP_16_BIT_PAUSE;
      sb16_curr_cont_cmd = DSP_16_BIT_CONT;
      sb16_curr_ack_cmd = DSP_16_BIT_ACK;
   }

   sound_fmt |= (p->channels == 1 ? DSP_MONO : DSP_STEREO);
   sound_fmt |= (p->sign ? DSP_SIGNED : DSP_UNSIGNED);

   sb16_set_sample_rate(p->sample_rate);

   outb(DSP_WRITE, prog_mode);
   outb(DSP_WRITE, sound_fmt);
   outb(DSP_WRITE, ((samples_cnt-1)     ) & 0xff); // count low
   outb(DSP_WRITE, ((samples_cnt-1) >> 8) & 0xff); // count high
}
