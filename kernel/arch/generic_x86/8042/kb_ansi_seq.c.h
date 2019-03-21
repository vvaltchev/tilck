/* SPDX-License-Identifier: BSD-2-Clause */


static const struct {

   u32 key;
   char seq[8];

} ansi_sequences[] = {

   {KEY_UP,                "\033[A"},
   {KEY_DOWN,              "\033[B"},
   {KEY_RIGHT,             "\033[C"},
   {KEY_LEFT,              "\033[D"},

   {KEY_NUMPAD_UP,         "\033[A"},
   {KEY_NUMPAD_DOWN,       "\033[B"},
   {KEY_NUMPAD_RIGHT,      "\033[C"},
   {KEY_NUMPAD_LEFT,       "\033[D"},

   {KEY_PAGE_UP,           "\033[5~"},
   {KEY_NUMPAD_PAGE_UP,    "\033[5~"},
   {KEY_PAGE_DOWN,         "\033[6~"},
   {KEY_NUMPAD_PAGE_DOWN,  "\033[6~"},

   {KEY_INS,               "\033[2~"},
   {KEY_NUMPAD_INS,        "\033[2~"},
   {KEY_DEL,               "\033[3~"},
   {KEY_NUMPAD_DEL,        "\033[3~"},
   {KEY_HOME,              "\033[H"},
   {KEY_NUMPAD_HOME,       "\033[H"},
   {KEY_END,               "\033[F"},
   {KEY_NUMPAD_END,        "\033[F"},

   {KEY_F1,                "\033[[A"},
   {KEY_F2,                "\033[[B"},
   {KEY_F3,                "\033[[C"},
   {KEY_F4,                "\033[[D"},
   {KEY_F5,                "\033[[E"},
   {KEY_F6,                "\033[17~"},
   {KEY_F7,                "\033[18~"},
   {KEY_F8,                "\033[19~"},
   {KEY_F9,                "\033[20~"},
   {KEY_F10,               "\033[21~"},
   {KEY_F11,               "\033[23~"},
   {KEY_F12,               "\033[24~"}
};

bool kb_scancode_to_ansi_seq(u32 key, u8 modifiers, char *seq)
{
   const char *base_seq = NULL;
   char *p;
   u32 sl;

   ASSERT(modifiers < 8);

   /*
    * It might not seem very efficient to linearly walk through all the
    * sequences trying to find the one for `key`. But that's just a perception:
    *
    * It's about doing ~30 iterations in the WORST case. For such small values
    * of N, it's almost always faster to use a linear algorithm than complex
    * O(1) or O(log(N)) algorithms, because they have significant (hidden)
    * constants multipliers in the function of N describing their *real*, not
    * asymptotic, cost.
    */
   for (u32 i = 0; i < ARRAY_SIZE(ansi_sequences); i++) {
      if (ansi_sequences[i].key == key) {
         base_seq = ansi_sequences[i].seq;
         break;
      }
   }

   if (!base_seq)
      return false;

   sl = strlen(base_seq);
   memcpy(seq, base_seq, sl + 1);

   if (!modifiers)
      return true;

   if (UNLIKELY(seq[1] != '[')) {

      if (seq[1] == 'O') {

         /*
          * The sequence is like F1: \033OP. Before appending the modifiers,
          * we need to replace 'O' with [1.
          */

         char end_char = seq[2];

         seq[1] = '[';
         seq[2] = '1';
         seq[3] = end_char;
         seq[4] = 0;

         sl++; /* we increased the size by 1 */

      } else {

         /* Don't know how to deal with such a sequence */
         return false;
      }

   } else if (!isdigit(seq[2])) {

      /*
       * Here the seq starts with ESC [, but a non-digit follows '['.
       * Example: the HOME key: \033[H
       * In this case, we need to insert a "1;" between '[' and 'H'.
       */

      if (sl != 3) {
         /* Don't support UNKNOWN cases like \033[XY: they should not exist */
         return false;
      }

      char end_char = seq[2];
      seq[2] = '1';
      seq[3] = end_char;
      seq[4] = 0;
      sl++;

      /* Now we have seq like \033[1H: the code below will complete the job */
   }

   /*
    * If the seq is like ESC [ <something> <end char>, then we can just
    * insert after "; <modifiers+1>" between <something> and <end char>.
    */

   char end_char = seq[sl - 1];

   p = &seq[sl - 1];
   *p++ = ';';
   *p++ = '1' + (char)modifiers;
   *p++ = end_char;
   *p = 0;

   return true;
}
