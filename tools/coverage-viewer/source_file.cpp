/* SPDX-License-Identifier: BSD-2-Clause */

#include "source_file.h"

#include <fstream>

static std::string
expand_tabs(const std::string &in)
{
   std::string out;
   out.reserve(in.size());

   for (char c : in) {

      if (c == '\t') {

         do {
            out.push_back(' ');
         } while (out.size() % 8 != 0);

      } else if (c != '\r') {

         out.push_back(c);
      }
   }

   return out;
}

void
source_file::load(const std::string &path)
{
   std::ifstream f(path);

   if (!f) {
      loaded = false;
      return;
   }

   std::string line;

   while (std::getline(f, line))
      lines.push_back(expand_tabs(line));

   loaded = true;
}
