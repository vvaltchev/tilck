/* SPDX-License-Identifier: BSD-2-Clause */

#include "model.h"
#include "util.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sys/stat.h>

double
cov_rate(int hit, int total)
{
   return total > 0 ? 100.0 * hit / total : 0.0;
}

bucket
cov_bucket(int hit, int total)
{
   if (total <= 0)
      return bucket::none;

   const double p = cov_rate(hit, total);

   if (p >= 90.0)
      return bucket::hi;

   if (p >= 75.0)
      return bucket::med;

   return bucket::lo;
}

/* Split "<num>,<rest>" into the leading long long and the rest. */
static bool
split_num_comma(const std::string &s, long long &num, std::string &rest)
{
   const size_t comma = s.find(',');

   if (comma == std::string::npos)
      return false;

   num = std::strtoll(s.c_str(), nullptr, 10);
   rest = s.substr(comma + 1);
   return true;
}

static void
format_file_mtime(const std::string &path, std::string &out)
{
   struct stat st;

   if (stat(path.c_str(), &st) != 0) {
      out = "(unknown)";
      return;
   }

   char buf[32];
   struct tm tm_buf;
   const time_t t = st.st_mtime;

   localtime_r(&t, &tm_buf);
   strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
   out = buf;
}

/*
 * Second pass: make paths relative to the common root, group files into
 * directories, and compute per-file / per-dir / grand totals.
 */
static void
aggregate(coverage_model &m)
{
   std::vector<std::string> abs_paths;
   abs_paths.reserve(m.files.size());

   for (const file_cov &f : m.files)
      abs_paths.push_back(f.abs_path);

   const std::string prefix = util::common_dir_prefix(abs_paths);
   std::map<std::string, int> dir_index;

   for (size_t i = 0; i < m.files.size(); i++) {

      file_cov &f = m.files[i];

      f.rel_path = util::starts_with(f.abs_path, prefix.c_str())
                      ? f.abs_path.substr(prefix.size())
                      : f.abs_path;
      f.dir = util::dir_of(f.rel_path);
      f.name = util::base_of(f.rel_path);

      f.lf = static_cast<int>(f.line_hits.size());
      f.lh = 0;

      for (const auto &kv : f.line_hits)
         if (kv.second > 0)
            f.lh++;

      f.fnf = static_cast<int>(f.funcs.size());
      f.fnh = 0;

      for (const func_cov &fn : f.funcs)
         if (fn.hits > 0)
            f.fnh++;

      auto it = dir_index.find(f.dir);

      if (it == dir_index.end()) {
         it = dir_index.emplace(f.dir, static_cast<int>(m.dirs.size())).first;
         m.dirs.emplace_back();
         m.dirs.back().path = f.dir;
      }

      dir_cov &d = m.dirs[it->second];
      d.files.push_back(static_cast<int>(i));
      d.lf += f.lf;
      d.lh += f.lh;
      d.fnf += f.fnf;
      d.fnh += f.fnh;

      m.lf += f.lf;
      m.lh += f.lh;
      m.fnf += f.fnf;
      m.fnh += f.fnh;
   }

   /* Default order: directories and files sorted by name. */
   std::sort(m.dirs.begin(), m.dirs.end(),
             [](const dir_cov &a, const dir_cov &b) {
                return a.path < b.path;
             });

   for (dir_cov &d : m.dirs)
      std::sort(d.files.begin(), d.files.end(),
                [&m](int a, int b) {
                   return m.files[a].name < m.files[b].name;
                });
}

bool
load_coverage(const std::string &path, coverage_model &m, std::string &err)
{
   std::ifstream f(path);

   if (!f) {
      err = "cannot open '" + path + "'";
      return false;
   }

   m.info_path = path;

   file_cov *cur = nullptr;
   std::map<std::string, size_t> fn_idx;   /* per-file name -> func index */
   std::string line;

   while (std::getline(f, line)) {

      if (!line.empty() && line.back() == '\r')
         line.pop_back();

      if (util::starts_with(line, "SF:")) {

         m.files.emplace_back();
         cur = &m.files.back();
         cur->abs_path = line.substr(3);
         fn_idx.clear();

      } else if (line == "end_of_record") {

         cur = nullptr;

      } else if (util::starts_with(line, "TN:")) {

         if (m.test_name.empty())
            m.test_name = line.substr(3);

      } else if (!cur) {

         continue;

      } else if (util::starts_with(line, "DA:")) {

         long long count;
         std::string rest;

         if (split_num_comma(line.substr(3), count, rest))
            cur->line_hits[static_cast<int>(count)] =
               std::strtoll(rest.c_str(), nullptr, 10);

      } else if (util::starts_with(line, "FNDA:")) {

         long long count;
         std::string name;

         if (split_num_comma(line.substr(5), count, name)) {

            auto it = fn_idx.find(name);

            if (it != fn_idx.end())
               cur->funcs[it->second].hits = count;
            else {
               fn_idx[name] = cur->funcs.size();
               cur->funcs.push_back(func_cov{name, 0, count});
            }
         }

      } else if (util::starts_with(line, "FN:")) {

         long long fline;
         std::string name;

         if (split_num_comma(line.substr(3), fline, name) &&
             fn_idx.find(name) == fn_idx.end())
         {
            fn_idx[name] = cur->funcs.size();
            cur->funcs.push_back(func_cov{name, static_cast<int>(fline), -1});
         }
      }
      /* LF/LH/FNF/FNH/BRDA and the rest are recomputed/ignored. */
   }

   if (m.test_name.empty())
      m.test_name = util::base_of(path);

   format_file_mtime(path, m.date);
   aggregate(m);
   return true;
}
