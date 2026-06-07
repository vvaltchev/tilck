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
#include <cstring>
#include <string>

#ifndef COVVIEW_BUILD_DIR
#define COVVIEW_BUILD_DIR "."
#endif

struct cli_args {
   std::string info_path;
   bool dump = false;
   bool help = false;
   bool bad = false;
};

static cli_args
parse_args(int argc, char **argv)
{
   cli_args a;

   for (int i = 1; i < argc; i++) {

      const std::string arg = argv[i];

      if (arg == "-h" || arg == "--help")
         a.help = true;
      else if (arg == "--dump")
         a.dump = true;
      else if (!arg.empty() && arg[0] == '-')
         a.bad = true;
      else
         a.info_path = arg;
   }

   if (a.info_path.empty())
      a.info_path = std::string(COVVIEW_BUILD_DIR) + "/coverage.info";

   return a;
}

static void
usage(const char *prog)
{
   printf("Usage: %s [OPTIONS] [coverage.info]\n\n", prog);
   printf("Browse LCOV coverage data in the terminal, a functional\n");
   printf("equivalent of the genhtml HTML report.\n\n");
   printf("Arguments:\n");
   printf("  coverage.info    LCOV tracefile to open\n");
   printf("                   (default: %s/coverage.info)\n\n",
          COVVIEW_BUILD_DIR);
   printf("Options:\n");
   printf("  -h, --help       show this help and exit\n");
   printf("      --dump       print parsed totals and exit (no UI)\n\n");
   printf("Keys:\n");
   printf("  arrows / j k     move          Enter / l   open\n");
   printf("  PgUp PgDn g G    page / ends    Backspace   back\n");
   printf("  Tab / f          source<->funcs  s          cycle sort\n");
   printf("  h l              pan source     ? / q       help / quit\n");
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

   if (args.bad) {
      usage(argv[0]);
      return 1;
   }

   if (args.help) {
      usage(argv[0]);
      return 0;
   }

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
