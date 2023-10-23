/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/tilck_sound.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>

#include "sb16.h"

/* One-time sb16 configuration shared with sb16_hw.c */
struct sb16_info sb16_info;

/* Device's MAJOR number */
static u16 sb16_major;

/*
 * Shared state between the IRQ handler and the rest of the code.
 *
 * NOTE: it's not necessary to use atomics because most of the time we disable
 * the interrupts while accessing the following state variables. In other cases,
 * like in sb16_ioctl_wait_for_completion() do just poll on sb16_playing:
 * volatile is mandatory, but no need for atomics. It's worth remarking that
 * in the simple model used by this driver, only ONE task at a time can acquire
 * and use this sound device. Therefore, no need for need for any kind of fancy
 * synchronization mechanisms.
 */
static volatile bool producer_is_sleeping;
static volatile bool sb16_have_slot[2];
static volatile bool sb16_playing;
static volatile u8 sb16_slot;

/* DSP config */
static struct tilck_sound_params dsp_params;
static u32 curr_buf_sz;

/* The task currently owning the sound device */
static struct task *owner;

static int
sb16_alloc_buf(void)
{
   size_t sz = 64 * KB;
   sb16_info.buf = general_kmalloc(&sz, KMALLOC_FL_DMA);

   if (!sb16_info.buf)
      return -ENOMEM;

   sb16_info.buf_paddr = LIN_VA_TO_PA(sb16_info.buf);

   /* The buffer must be aligned at 64-KB boundary */
   ASSERT((sb16_info.buf_paddr & (64 * KB - 1)) == 0);
   return 0;
}

static enum irq_action
sb16_handle_irq(void *ctx)
{
   SB16_DBG("sb16, irq, completed slot: %u\n", sb16_slot);

   /* Mark the current slot as "used" */
   sb16_have_slot[sb16_slot] = false;

   /* Switch to the other slot */
   sb16_slot = !sb16_slot;

   /* ACK the hardware */
   sb16_irq_ack();

   if (!sb16_have_slot[sb16_slot]) {

      SB16_DBG("sb16, irq, no data for slot %u: STOP\n", sb16_slot);
      /* We don't have data ready in the new slot, stop */
      sb16_pause();
      sb16_playing = false;

   } else {

      SB16_DBG("sb16, irq, switch to slot: %u\n", sb16_slot);
   }

   if (producer_is_sleeping) {
      if (owner && owner->state == TASK_STATE_SLEEPING)
         task_change_state(owner, TASK_STATE_RUNNABLE);
   }

   return IRQ_HANDLED;
}

DEFINE_IRQ_HANDLER_NODE(dsp_irq_node, &sb16_handle_irq, NULL);


static int
sb16_install_irq_handler(void)
{
   sb16_info.irq = sb16_get_irq();
   printk("sb16: using irq #%u\n", sb16_info.irq);
   irq_install_handler(sb16_info.irq, &dsp_irq_node);
   return 0;
}

static ssize_t
sb16_read(fs_handle h, char *user_buf, size_t size, offt *pos)
{
   return 0;
}

static void
sb16_fill_buf_with_mute(void *buf, size_t len)
{
   u16 mute = (u16)(dsp_params.sign ? 0 : (1 << ((u32)dsp_params.bits-1)) - 1);

   if (dsp_params.bits == 8)
      memset(buf, mute, len);
   else
      memset16(buf, mute, len / 2);
}

static void
sb16_fill_slot_with_mute(u8 slot)
{
   u8 *buf = sb16_info.buf + (slot << 15);
   sb16_fill_buf_with_mute(buf, 32 * KB);
}

static void
do_start_play(u8 next_slot, u32 sz)
{
   static bool played_anything;

   bool currently_playing;

   disable_interrupts_forced();
   {
      /* Mark the data in the next slot as "ready" */
      SB16_DBG("write(): mark slot %u as available\n", next_slot);
      sb16_have_slot[next_slot] = true;
      currently_playing = sb16_playing;
   }
   enable_interrupts_forced();

   if (!currently_playing) {

      SB16_DBG("write(): not playing, START!\n");
      ASSERT(next_slot == 0);
      sb16_slot = next_slot;
      sb16_have_slot[1] = false;
      sb16_fill_slot_with_mute(1);

      sb16_playing = true;
      curr_buf_sz = MAX(sz, played_anything ? 4 * KB : 8 * KB);
      played_anything = true;

      sb16_program_dma(dsp_params.bits, sz < 32 * KB ? sz : 64 * KB);
      sb16_program(&dsp_params, curr_buf_sz);

   } else {

      SB16_DBG("write(): playing slot %u, do nothing\n", !next_slot);
   }
}

static ssize_t
sb16_write(fs_handle h, char *user_buf, size_t size, offt *pos)
{
   if (get_curr_task() != owner) {
      /* The current task, does not own the resource */
      return -EPERM;
   }

   if (!dsp_params.bits) {
      /* Audio hasn't been configured with TILCK_IOCTL_SOUND_SETUP yet */
      return -EINVAL;
   }

   const size_t sz = MIN(size, 32 * KB);
   bool must_sleep;
   u8 next_slot;
   u8 *dest_buf;

   do {

      must_sleep = false;
      disable_interrupts_forced();
      {
         if (sb16_playing) {

            next_slot = !sb16_slot;

            if (sb16_have_slot[next_slot] || curr_buf_sz < 32 * KB) {

               if (curr_buf_sz < 32 * KB)
                  SB16_DBG("force sleep on write() because buf < 32KB\n");

               must_sleep = true;
            }

         } else {
            next_slot = 0;
         }
      }
      enable_interrupts_forced();

      if (must_sleep) {

         SB16_DBG("write() requires to sleep (waiting for slot)\n");

         producer_is_sleeping = true;
         kernel_sleep_ms(100);
         task_cancel_wakeup_timer(get_curr_task());

         if (pending_signals())
            return -EINTR;

         producer_is_sleeping = false;
      }

   } while (must_sleep);

   SB16_DBG("write(): using slot: %u\n", next_slot);
   dest_buf = sb16_info.buf + (next_slot << 15);

   if (copy_from_user(dest_buf, user_buf, sz))
      return -EFAULT;

   if (sz < 32 * KB) {

      if (dsp_params.bits == 16 || dsp_params.channels == 2) {

         if (sz % 2)
            return -EINVAL;

         if (dsp_params.bits == 16 && dsp_params.channels == 2)
            if (sz % 4)
               return -EINVAL;
      }

      SB16_DBG("write(): small buf %u, fill %u with mute\n", sz, 32*KB - sz);
      sb16_fill_buf_with_mute(dest_buf + sz, 32 * KB - sz);
   }

   do_start_play(next_slot, sz);
   return (ssize_t)sz;
}

static int
sb16_ioctl_sound_setup(struct tilck_sound_params *user_params)
{
   if (get_curr_task() != owner) {
      /* The current task, does not own the resource */
      return -EPERM;
   }

   if (sb16_playing)
      return -EBUSY;

   if (copy_from_user(&dsp_params, user_params, sizeof(dsp_params)))
      return -EFAULT;

   if (dsp_params.sample_rate <= 44100 &&
       (dsp_params.bits == 8 || dsp_params.bits == 16) &&
       (dsp_params.channels == 1 || dsp_params.channels == 2) &&
       (dsp_params.sign == 0 || dsp_params.sign == 1))
   {
      sb16_have_slot[0] = false;
      sb16_have_slot[1] = false;
      sb16_fill_slot_with_mute(0);
      sb16_fill_slot_with_mute(1);
      return 0;
   }

   bzero(&dsp_params, sizeof(dsp_params));
   return -EINVAL;
}

static void
sb16_release_on_exit(struct task *ti)
{
   ASSERT(!is_preemption_enabled());
   disable_interrupts_forced();
   {
      owner = NULL;
   }
   enable_interrupts_forced();
   SB16_DBG("sb16: release ownership from TID: %d\n", ti->tid);
}

static int
sb16_ioctl_acquire(void)
{
   int rc = 0;
   disable_preemption();
   {
      if (owner && get_curr_task() != owner) {

         /* The current task, does not own the resource */
         rc = -EPERM;

      } else {

         rc = register_on_task_exit_cb(&sb16_release_on_exit);

         if (rc >= 0)
            owner = get_curr_task();
      }
   }
   enable_preemption();
   return rc;
}

static int
sb16_ioctl_release(void)
{
   int rc = 0;
   disable_preemption();
   {
      if (get_curr_task() != owner) {

         /* The current task does not own the resource */
         rc = -EPERM;

      } else {

         unregister_on_task_exit_cb(&sb16_release_on_exit);
         owner = NULL;
      }
   }
   enable_preemption();
   return rc;
}

static int
sb16_ioctl_get_info(struct tilck_sound_card_info *user_info)
{
   static const struct tilck_sound_card_info info = {
      .name = "sb16",
      .max_sample_rate = 44100,
      .max_bits = 16,
      .max_channels = 2,
   };

   if (copy_to_user(user_info, &info, sizeof(info)))
      return -EFAULT;

   return 0;
}

static int
sb16_ioctl_wait_for_completion(void)
{
   if (get_curr_task() != owner) {
      /* The current task does not own the resource */
      return -EPERM;
   }

   while (sb16_playing) {

      producer_is_sleeping = true;
      kernel_sleep_ms(100);
      task_cancel_wakeup_timer(get_curr_task());

      if (pending_signals())
         return -EINTR;

      producer_is_sleeping = false;
   }

   return 0;
}

static int
sb16_ioctl(fs_handle h, ulong request, void *user_argp)
{
   switch (request) {

      case TILCK_IOCTL_SOUND_ACQUIRE:
         return sb16_ioctl_acquire();

      case TILCK_IOCTL_SOUND_RELEASE:
         return sb16_ioctl_release();

      case TILCK_IOCTL_SOUND_SETUP:
         return sb16_ioctl_sound_setup(user_argp);

      case TILCK_IOCTL_SOUND_GET_INFO:
         return sb16_ioctl_get_info(user_argp);

      case TILCK_IOCTL_SOUND_WAIT_COMPLETION:
         return sb16_ioctl_wait_for_completion();

      default:
         return -EINVAL;
   }
}

static int
create_sb16_device(int minor,
                   enum vfs_entry_type *type,
                   struct devfs_file_info *nfo)
{
   static const struct file_ops static_ops_sb16 = {
      .read = sb16_read,
      .write = sb16_write,
      .ioctl = sb16_ioctl,
   };

   *type = VFS_CHAR_DEV;
   nfo->fops = &static_ops_sb16;
   nfo->spec_flags = VFS_SPFL_NO_USER_COPY;
   return 0;
}

static void
init_sb16(void)
{
   int rc;

   if (sb16_detect_dsp_hw_and_reset() < 0)
      return;

   if (sb16_check_version() < 0)
      return;

   printk("sb16: hw init success, version: %u.%u\n",
          sb16_info.ver_major, sb16_info.ver_minor);

   if (sb16_install_irq_handler() < 0)
      return;

   if (sb16_alloc_buf() < 0) {
      printk("sb16: failed to alloc buffer\n");
      return;
   }

   outb(DSP_WRITE, DSP_ENABLE_SPKR);

   struct driver_info *di = kalloc_obj(struct driver_info);

   if (!di) {
      printk("sb16: out of memory\n");
      return;
   }

   di->name = "sb16";
   di->create_dev_file = create_sb16_device;
   rc = register_driver(di, -1);

   if (rc < 0) {
      printk("sb16: failed to register driver (%d)\n", rc);
      return;
   }

   sb16_major = (u16) rc;
   rc = create_dev_file("sb16", sb16_major, 0 /* minor */, NULL);

   if (rc != 0)
      panic("sb16: unable to create /dev/sb16 (error: %d)", rc);
}

static struct module sb16_module = {

   .name = "sb16",
   .priority = MOD_sb16_prio,
   .init = &init_sb16,
};

REGISTER_MODULE(&sb16_module);
