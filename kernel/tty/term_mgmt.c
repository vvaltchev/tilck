/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/term.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/tty_struct.h>

struct term *__curr_term;
const struct term_interface *__curr_term_intf;
const struct term_interface *video_term_intf;
const struct term_interface *serial_term_intf;

void set_curr_video_term(struct term *t)
{
   ASSERT(!is_preemption_enabled());
   ASSERT(__curr_term_intf != NULL);
   ASSERT(__curr_term_intf->get_type() == term_type_video);

   __curr_term_intf->pause_video_output(__curr_term);
   __curr_term = t;
   __curr_term_intf->restart_video_output(__curr_term);
}

void register_term_intf(const struct term_interface *intf)
{
   switch (intf->get_type()) {

      case term_type_video:
         ASSERT(video_term_intf == NULL);
         video_term_intf = intf;
         break;

      case term_type_serial:
         ASSERT(serial_term_intf == NULL);
         serial_term_intf = intf;
         break;

      default:
         NOT_REACHED();
   }
}


void
init_first_video_term(const struct video_interface *vi,
                     u16 rows,
                     u16 cols,
                     int rows_buf)
{
   ASSERT(!term_is_initialized());
   __curr_term_intf = video_term_intf;
   __curr_term = __curr_term_intf->get_first_term();
   __curr_term_intf->video_term_init(__curr_term, vi, rows, cols, rows_buf);
}

void
init_first_serial_term(u16 port)
{
   ASSERT(!term_is_initialized());
   __curr_term_intf = serial_term_intf;
   __curr_term = __curr_term_intf->get_first_term();
   __curr_term_intf->serial_term_init(__curr_term, port);
}

void init_first_term_null(void)
{
   init_first_video_term(NULL, 0, 0, 0);
}

void process_term_read_info(struct term_params *out)
{
   struct tty *t = get_curr_process_tty();
   ASSERT(__curr_term_intf);

   if (t)
      *out = t->tparams;
   else
      __curr_term_intf->get_params(__curr_term, out);
}
