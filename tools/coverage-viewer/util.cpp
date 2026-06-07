/* SPDX-License-Identifier: BSD-2-Clause */

#include "util.h"

namespace util {

std::string
common_dir_prefix(const std::vector<std::string> &paths)
{
   if (paths.empty())
      return std::string();

   std::string lcp = paths[0];

   for (const std::string &s : paths) {

      size_t i = 0;

      while (i < lcp.size() && i < s.size() && lcp[i] == s[i])
         i++;

      lcp.resize(i);
   }

   /* Keep the prefix up to and including the last '/'. */
   const size_t slash = lcp.rfind('/');
   return slash == std::string::npos ? std::string() : lcp.substr(0, slash + 1);
}

} /* namespace util */
