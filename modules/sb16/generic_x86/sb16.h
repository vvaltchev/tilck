/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>

#define SB16_DBG_ENABLED       0

#define DSP_MIXER          0x224
#define DSP_MIXER_DATA     0x225
#define DSP_RESET          0x226
#define DSP_READ           0x22A
#define DSP_WRITE          0x22C
#define DSP_READ_STATUS    0x22E

#define DSP_8_BIT_ACK      DSP_READ_STATUS
#define DSP_16_BIT_ACK     0x22F

#define DSP_16_BIT_PROG    0xB0
#define DSP_8_BIT_PROG     0xC0
#define DSP_AUTO_INIT      0x06
#define DSP_PLAY           0x00
#define DSP_RECORD         0x08
#define DSP_MONO           0x00
#define DSP_STEREO         0x20
#define DSP_UNSIGNED       0x00
#define DSP_SIGNED         0x10

#define DSP_SET_SAMPLE_R   0x41
#define DSP_ENABLE_SPKR    0xD1
#define DSP_DISABLE_SPKR   0xD3
#define DSP_GET_VER        0xE1
#define DSP_8_BIT_PAUSE    0xD0
#define DSP_8_BIT_CONT     0xD4
#define DSP_16_BIT_PAUSE   0xD5
#define DSP_16_BIT_CONT    0xD6

struct sb16_info {
   u8 *buf;
   ulong buf_paddr;
   u8 irq;
   u8 ver_major;
   u8 ver_minor;
};

extern struct sb16_info sb16_info;

u8 sb16_get_irq(void);
int sb16_detect_dsp_hw_and_reset(void);
int sb16_check_version(void);
void sb16_program_dma(u8 bits, u32 buf_sz);
void sb16_program(struct tilck_sound_params *params, u32 buf_sz);
void sb16_generate_test_sound(void);

static inline void sb16_irq_ack(void)
{
   extern u16 sb16_curr_ack_cmd;
   inb(sb16_curr_ack_cmd);
}

static inline void sb16_pause(void)
{
   extern u8 sb16_curr_pause_cmd;
   outb(DSP_WRITE, sb16_curr_pause_cmd);
}

static inline void sb16_continue(void)
{
   extern u8 sb16_curr_cont_cmd;
   outb(DSP_WRITE, sb16_curr_cont_cmd);
}


#if SB16_DBG_ENABLED
   #define SB16_DBG(...) printk(__VA_ARGS__)
#else
   #define SB16_DBG(...) { }
#endif
