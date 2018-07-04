
#include <exos/common/basic_defs.h>

typedef struct {

   u32 width;
   u32 height;

} display_resolution;

static const display_resolution exos_known_resolutions[] =
{
   {640, 480},
   {800, 600},
   {1024, 600},
   {1024, 768},
   {1280, 1024},
   {1920, 1080}
};

bool is_exos_known_resolution(u32 w, u32 h)
{
   const display_resolution *kr = exos_known_resolutions;

   for (u32 i = 0; i < ARRAY_SIZE(exos_known_resolutions); i++) {
      if (kr[i].width == w && kr[i].height == h)
         return true;
   }

   return false;
}
