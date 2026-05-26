/* SPDX-License-Identifier: BSD-2-Clause */

enum log_level {
   LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL
};

int classify(enum log_level lvl)
{
   return (int) lvl;
}
