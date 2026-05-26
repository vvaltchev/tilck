/* SPDX-License-Identifier: BSD-2-Clause */

/* One enumerator per line: compliant -- rule does not fire. */
enum log_level {
   LOG_DEBUG,
   LOG_INFO,
   LOG_WARN,
   LOG_ERROR,
   LOG_FATAL
};

/* Aligned-values form: also compliant. */
enum status_code {
   STATUS_OK    = 0,
   STATUS_BUSY  = 1,
   STATUS_DONE  = 2
};

/* Single-enumerator enum -- rule should skip (< 2 children). */
enum single {
   ONE
};

int main(void)
{
   return STATUS_OK + LOG_DEBUG + ONE;
}
