/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <string>
#include <vector>

/*
 * A source file loaded for the source view: its lines with tabs expanded
 * (width 8, like genhtml). `loaded` is false when the file can't be read,
 * in which case the source view still shows line/hit data with empty
 * text. Line N of the file is lines[N - 1].
 */
struct source_file {
   bool loaded = false;
   std::vector<std::string> lines;

   void load(const std::string &path);
};
