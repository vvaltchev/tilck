
How to measure the code coverage
----------------------------------

## Prerequisites

 * Some experience with the scripts `cmake_run` and `build_toolchain`.
   Read [building](building.md) first.

 * A clean build directory

 * The following extra components in the toolchain:

      - lcov
      - gtest
      - vim (advanced case only)

## Typical case

By following the steps below, you'll measure Tilck's cumulative test coverage
for the unit tests, for the kernel self-tests and the system tests as well.
That's what is needed most of the time. For the full coverage, the interactive
tests need to be run as well. They have more prerequisites (see above) and
require a special build configuration as well (see below).

1. Setup the build configuration by running:

        TEST_GCOV=1 KERNEL_GCOV=1 <TILCK>/scripts/cmake_run

2. Build Tilck and its unit tests:

        make && make gtests

3. Clean coverage data from previous runs:

        <BUILD_DIR>/scripts/generate_test_coverage_report --clean

4. Run the unit tests:

        <BUILD_DIR>/gtests

   At this point, there should be plenty of `.gcda` files in the build
   directory. Check that with: `find <BUILD_DIR> -name '*.gcda'`

5. Generate the first coverage report with:

        <BUILD_DIR>/scripts/generate_test_coverage_report --acc

   At this point, there should be a `coverage.info` file in the build directory,
   but we're not done yet. We have only coverage for the **unit tests**.

6. In order to get coverage info for the **kernel self-tests** and the
   **system tests**, we need to run also:

        DUMP_COV=1 REPORT_COV=1 <BUILD_DIR>/st/run_all_tests -c

7. At this point, our `coverage.info` file will contain the merged coverage data
   from both the unit tests run and all the other tests (where kernel coverage
   was involved as well). Therefore, we can finally generate our HTML report
   this way:

        <BUILD_DIR>/scripts/generate_test_coverage_report --gen

   Open `<BUILD_DIR>/coverage_html/index.html` in your browser.

## Advanced case

In order to get the *full* test coverage, it is necessary to run also Tilck's
interactive tests as well. (The *interactive* tests simulate real user
input (keystrokes on a virtual PS/2 keyboard) and check Tilck's fb console's
output by parsing screenshots.) To run them and get the full test coverage
it's necessary to:

 * Check that you have the [pySerial] python-3 module installed on the system

 * Check that you have [ImageMagick] installed on the system

 * Replace the **step 1** above with:

        TEST_GCOV=1 KERNEL_GCOV=1 <TILCK>/scripts/cmake_run --intr

 * Run the following command **between step 6 and step 7**:

        DUMP_COV=1 REPORT_COV=1 <BUILD_DIR>/st/run_interactive_test -a

The rest of the steps are the same. We just changed build's configuration and
run another test before generating the html report.

[pySerial]: https://pyserial.readthedocs.io/en/latest/pyserial.html
[ImageMagick]: https://imagemagick.org/
