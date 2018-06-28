
#define KB_CBUF_SIZE 256

static char kb_cooked_buf[256];
static ringbuf kb_cooked_ringbuf;

bool kb_cbuf_is_empty(void)
{
   return ringbuf_is_empty(&kb_cooked_ringbuf);
}

bool kb_cbuf_is_full(void)
{
   return ringbuf_is_full(&kb_cooked_ringbuf);
}

char kb_cbuf_read_elem(void)
{
   u8 ret;
   ASSERT(!kb_cbuf_is_empty());
   DEBUG_CHECKED_SUCCESS(ringbuf_read_elem1(&kb_cooked_ringbuf, &ret));
   return (char)ret;
}

static ALWAYS_INLINE bool kb_cbuf_drop_last_written_elem(char *c)
{
   char unused;

   if (!c)
      c = &unused;

   return ringbuf_unwrite_elem(&kb_cooked_ringbuf, c);
}

static ALWAYS_INLINE bool kb_cbuf_write_elem(char c)
{
   return ringbuf_write_elem1(&kb_cooked_ringbuf, c);
}
