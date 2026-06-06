/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <map>
#include <string>
#include <vector>

/*
 * In-memory coverage model, built from an LCOV tracefile (coverage.info).
 * It carries exactly the data the genhtml HTML report is derived from:
 * per-file line/function hit counts plus a flat per-directory
 * aggregation. See docs/plans/coverage-viewer-feature-spec.md.
 */

/* A line's coverage state. */
enum class line_state {
   none,      /* not instrumented (no DA record) */
   covered,   /* DA count > 0 */
   uncovered, /* DA count == 0 */
};

/* Coverage rate bucket (genhtml defaults: hi >= 90, med >= 75). */
enum class bucket {
   none,      /* nothing to measure (total == 0) */
   lo,
   med,
   hi,
};

struct func_cov {
   std::string name;       /* mangled function name */
   int line = 0;           /* start line in the source */
   long long hits = -1;    /* FNDA count; -1 means "no FNDA seen" -> 0 */
};

struct file_cov {
   std::string abs_path;                  /* SF: record (absolute) */
   std::string rel_path;                  /* relative to the report root */
   std::string dir;                       /* dir part of rel_path */
   std::string name;                      /* basename */
   std::map<int, long long> line_hits;    /* DA: line -> exec count */
   std::vector<func_cov> funcs;
   int lf = 0, lh = 0;                    /* lines found / hit */
   int fnf = 0, fnh = 0;                  /* functions found / hit */
};

struct dir_cov {
   std::string path;                      /* relative directory path */
   std::vector<int> files;                /* indices into model.files */
   int lf = 0, lh = 0;
   int fnf = 0, fnh = 0;
};

struct coverage_model {
   std::string info_path;                 /* the loaded coverage.info */
   std::string test_name;                 /* TN: (falls back to filename) */
   std::string date;                      /* info file mtime, formatted */
   std::vector<file_cov> files;
   std::vector<dir_cov> dirs;
   int lf = 0, lh = 0, fnf = 0, fnh = 0;  /* grand totals */
};

/* Rate helpers. total == 0 yields 0.0 and bucket::none. */
double cov_rate(int hit, int total);
bucket cov_bucket(int hit, int total);

/*
 * Parse an LCOV tracefile into `m`. On failure returns false and sets
 * `err`. On success the model is fully aggregated (rel paths, per-dir
 * sums, grand totals, date).
 */
bool load_coverage(const std::string &path, coverage_model &m,
                   std::string &err);
