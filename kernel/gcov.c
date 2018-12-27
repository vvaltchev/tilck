/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/fault_resumable.h>

typedef u64 gcov_type;
typedef u32 gcov_unsigned_t;

/*
 * Unfortunately, GCOV_COUNTERS changes with GCC's versions. Hopefully, the
 * following logic is correct and we've missed no cases.
 */

#if (__GNUC__ >= 7)
   #define GCOV_COUNTERS 9
#elif (__GNUC__ > 5) || (__GNUC__ == 5 && __GNUC_MINOR__ >= 1)
   #define GCOV_COUNTERS 10
#elif __GNUC__ == 4 && __GNUC_MINOR__ >= 9
   #define GCOV_COUNTERS 9
#else
   #define GCOV_COUNTERS 8
#endif

/*
 * --------------------------------------------------------------------------
 * Data structures (w/ comments) copied from GCC's source: libgcc/libgcov.h
 * --------------------------------------------------------------------------
 */


/* Information about counters for a single function.  */
struct gcov_ctr_info
{
  gcov_unsigned_t num;          /* number of counters.  */
  gcov_type *values;            /* their values.  */
};

/* Information about a single function.  This uses the trailing array
   idiom. The number of counters is determined from the merge pointer
   array in gcov_info.  The key is used to detect which of a set of
   comdat functions was selected -- it points to the gcov_info object
   of the object file containing the selected comdat function.  */

struct gcov_fn_info
{
  const struct gcov_info *key;          /* comdat key */
  gcov_unsigned_t ident;                /* unique ident of function */
  gcov_unsigned_t lineno_checksum;      /* function lineo_checksum */
  gcov_unsigned_t cfg_checksum;         /* function cfg checksum */
  struct gcov_ctr_info ctrs[1];         /* instrumented counters */
};

/* Type of function used to merge counters.  */
typedef void (*gcov_merge_fn) (gcov_type *, gcov_unsigned_t);

/* Information about a single object file.  */
struct gcov_info
{
  gcov_unsigned_t version;      /* expected version number */
  struct gcov_info *next;       /* link to next, used by libgcov */
  gcov_unsigned_t stamp;        /* uniquifying time stamp */
  const char *filename;         /* output file name */
  gcov_merge_fn merge[GCOV_COUNTERS];  /* merge functions (null for unused) */
  unsigned n_functions;         /* number of functions */
  const struct gcov_fn_info *const *functions; /* pointer to pointers
                                                  to function information  */
};

/*
 * --------------------------------------------------------------------------
 * END
 * --------------------------------------------------------------------------
 */

static struct gcov_info *gi_list;
static int files_count;

#if KERNEL_GCOV || defined(KERNEL_TEST)
   #define FILE_ARRAY_SIZE 1024
#else
   #define FILE_ARRAY_SIZE 1
#endif

static struct gcov_info *files_array[FILE_ARRAY_SIZE];

#if !defined(KERNEL_TEST)

void __gcov_merge_add(gcov_type *counters, u32 n) { }
void __gcov_exit(void) { }

void __gcov_init(struct gcov_info *info)
{
   if (!gi_list) {
      gi_list = info;
   } else {
      info->next = gi_list;
      gi_list = info;
   }

   files_array[files_count++] = info;
}

#endif

/*
 * Dump counters to a GCDA file.
 * Definitions taken from GCC's source: gcc/gcov-io.h.
 */

/* File magic. Must not be palindromes.  */
#define GCOV_DATA_MAGIC ((gcov_unsigned_t)0x67636461) /* "gcda" */


#define GCOV_TAG_FUNCTION        ((gcov_unsigned_t)0x01000000)
#define GCOV_TAG_FUNCTION_LENGTH (3)
#define GCOV_TAG_COUNTER_BASE    ((gcov_unsigned_t)0x01a10000)
#define GCOV_TAG_COUNTER_LENGTH(NUM) ((NUM) * 2)

/* Convert a counter index to a tag.  */
#define GCOV_TAG_FOR_COUNTER(COUNT)                             \
        (GCOV_TAG_COUNTER_BASE + ((gcov_unsigned_t)(COUNT) << 17))

static u32 words_dumped;

static void dump_begin(void)
{
   words_dumped = 0;
}

static void dump_u32(u32 val)
{
   if (words_dumped && !(words_dumped % 6))
      printk(NO_PREFIX "\n");

   printk(NO_PREFIX "%p ", val);
   words_dumped++;
}

static void dump_u64(u64 val)
{
   dump_u32(val & 0xffffffffull);
   dump_u32(val >> 32);
}

static void dump_end(void)
{
   printk(NO_PREFIX "\n");
}

static void dump_gcda(struct gcov_info *info)
{
   const struct gcov_fn_info *func;
   const struct gcov_ctr_info *counters;

   dump_begin();

   // Header
   dump_u32(GCOV_DATA_MAGIC);
   dump_u32(info->version);
   dump_u32(info->stamp);

   for (u32 i = 0; i < info->n_functions; i++) {

      func = info->functions[i];

      dump_u32(GCOV_TAG_FUNCTION);
      dump_u32(GCOV_TAG_FUNCTION_LENGTH);
      dump_u32(func->ident);
      dump_u32(func->lineno_checksum);
      dump_u32(func->cfg_checksum);

      counters = func->ctrs;

      for (u32 j = 0; j < GCOV_COUNTERS; j++) {

         if (!info->merge[j])
            continue; /* no merge func -> the counter is NOT used */

         dump_u32(GCOV_TAG_FOR_COUNTER(j));
         dump_u32(GCOV_TAG_COUNTER_LENGTH(counters->num));

         for (u32 k = 0; k < counters->num; k++)
            dump_u64(counters->values[j]);

         counters++;

      } // for (u32 j = 0; j < GCOV_COUNTERS; j++)
   } // for (u32 i = 0; i < info->n_functions; i++)

   dump_end();
}

void gcov_dump_coverage(void)
{
   struct gcov_info *ptr = gi_list;

   printk("** GCOV gcda files **\n");

   while (ptr) {
      printk(NO_PREFIX "\nfile: %s\n", ptr->filename);
      dump_gcda(ptr);
      ptr = ptr->next;
   }
}

// ----------------------------------------------------------------

static u32 compute_gcda_file_size(const struct gcov_info *info)
{
   const struct gcov_ctr_info *counters;

   u32 size = 0;

   // header
   size += 3 * sizeof(u32);

   for (u32 i = 0; i < info->n_functions; i++) {

      // function tag & checksums
      size += 5 * sizeof(u32);
      counters = info->functions[i]->ctrs;

      for (u32 j = 0; j < GCOV_COUNTERS; j++) {

         if (!info->merge[j])
            continue; /* no merge func -> the counter is NOT used */

         size += 2 * sizeof(u32);
         size += counters->num * sizeof(u64);
         counters++;
      }
   }

   return size;
}

// ----------------------------------------------------------------


int sys_gcov_dump_coverage(void)
{
   return files_count;
}

int sys_gcov_get_file_info(int fn,
                           char *user_fname_buf,
                           u32 fname_buf_size,
                           u32 *user_fsize)
{
   if (fn < 0 || fn >= files_count)
      return -EINVAL;

   int rc;
   const struct gcov_info *gi = files_array[fn];
   const u32 fname_len = strlen(gi->filename);

   if (fname_buf_size < fname_len + 1) {
      return -ENOBUFS;
   }

   rc = copy_to_user(user_fname_buf, gi->filename, fname_len + 1);

   if (rc != 0)
      return -EFAULT;

   u32 s = compute_gcda_file_size(gi);
   rc = copy_to_user(user_fsize, &s, sizeof(s));

   if (rc != 0)
      return -EFAULT;

   return 0;
}


static void gcov_dump_file_to_buf(const struct gcov_info *gi, void *buf)
{
   const struct gcov_fn_info *func;
   const struct gcov_ctr_info *counters;
   u32 *ptr = buf;

   // Header
   *ptr++ = GCOV_DATA_MAGIC;
   *ptr++ = gi->version;
   *ptr++ = gi->stamp;

   for (u32 i = 0; i < gi->n_functions; i++) {

      func = gi->functions[i];

      *ptr++ = GCOV_TAG_FUNCTION;
      *ptr++ = GCOV_TAG_FUNCTION_LENGTH;
      *ptr++ = func->ident;
      *ptr++ = func->lineno_checksum;
      *ptr++ = func->cfg_checksum;

      counters = func->ctrs;

      for (u32 j = 0; j < GCOV_COUNTERS; j++) {

         if (!gi->merge[j])
            continue; /* no merge func -> the counter is NOT used */

         *ptr++ = GCOV_TAG_FOR_COUNTER(j);
         *ptr++ = GCOV_TAG_COUNTER_LENGTH(counters->num);

         for (u32 k = 0; k < counters->num; k++) {
            u64 val = counters->values[j];
            *ptr++ = val & 0xffffffffull;
            *ptr++ = val >> 32;
         }

         counters++;

      } // for (u32 j = 0; j < GCOV_COUNTERS; j++)
   } // for (u32 i = 0; i < info->n_functions; i++)
}

int sys_gcov_get_file(int fn, char *user_buf)
{
   if (fn < 0 || fn >= files_count)
      return -EINVAL;

   int rc;
   const struct gcov_info *gi = files_array[fn];

   rc = fault_resumable_call(~0, &gcov_dump_file_to_buf, 2, gi, user_buf);

   if (rc != 0)
      return -EFAULT;

   return 0;
}
