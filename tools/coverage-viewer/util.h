/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <string>
#include <vector>

namespace util {

inline bool
starts_with(const std::string &s, const char *prefix)
{
   const size_t n = std::char_traits<char>::length(prefix);
   return s.size() >= n && s.compare(0, n, prefix) == 0;
}

/* Directory part of a relative path ("" when there is no '/'). */
inline std::string
dir_of(const std::string &path)
{
   const size_t slash = path.rfind('/');
   return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

/* Last path component. */
inline std::string
base_of(const std::string &path)
{
   const size_t slash = path.rfind('/');
   return slash == std::string::npos ? path : path.substr(slash + 1);
}

/*
 * Longest common directory prefix of a set of absolute paths, kept up to
 * and including the final '/' (so it can be stripped to make paths
 * relative). Mirrors what genhtml does to pick the report root.
 */
std::string common_dir_prefix(const std::vector<std::string> &paths);

} /* namespace util */
