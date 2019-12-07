/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/term.h>
#include <tilck/kernel/sched.h>

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
