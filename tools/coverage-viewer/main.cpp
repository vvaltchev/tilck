/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * coverage-viewer: a terminal UI to browse LCOV coverage data, a
 * functional equivalent of the genhtml HTML report (see
 * docs/plans/coverage-viewer-feature-spec.md). It reads an LCOV tracefile
 * (coverage.info) and the source files it references.
 */

#include "app.h"
#include "model.h"

#include <cstdio>
#include <string>

struct cli_args {
   std::string info_path = "coverage.info";
   bool dump = false;
};

static cli_args
parse_args(int argc, char **argv)
{
   cli_args a;

   for (int i = 1; i < argc; i++) {

      const std::string arg = argv[i];

      if (arg == "--dump")
         a.dump = true;
      else
         a.info_path = arg;
   }

   return a;
}

static void
dump_model(const coverage_model &m)
{
   printf("coverage.info: %s\n", m.info_path.c_str());
   printf("test: %s   date: %s\n", m.test_name.c_str(), m.date.c_str());
   printf("files: %zu   dirs: %zu\n", m.files.size(), m.dirs.size());
   printf("lines:     %d / %d  (%.1f %%)\n",
          m.lh, m.lf, cov_rate(m.lh, m.lf));
   printf("functions: %d / %d  (%.1f %%)\n",
          m.fnh, m.fnf, cov_rate(m.fnh, m.fnf));
}

int
main(int argc, char **argv)
{
   const cli_args args = parse_args(argc, argv);

   coverage_model m;
   std::string err;

   if (!load_coverage(args.info_path, m, err)) {
      fprintf(stderr, "coverage-viewer: %s\n", err.c_str());
      return 1;
   }

   if (args.dump) {
      dump_model(m);
      return 0;
   }

   app a(m);
   a.run();
   return 0;
}
