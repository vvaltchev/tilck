
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

        <TILCK>/scripts/cmake_run --gcov

   (`--gcov` just exports `TEST_GCOV=1 KERNEL_GCOV=1` for you; the explicit
   `TEST_GCOV=1 KERNEL_GCOV=1 <TILCK>/scripts/cmake_run` form still works.)

2. Build Tilck and its unit tests:

        make && make gtests

3. Run every test type and collect the merged coverage:

        <BUILD_DIR>/st/run_all_tests --coverage --html

   That single command runs the unit tests, the kernel self-tests and the
   system tests (and the interactive tests too, see below), collecting their
   coverage into a single `<BUILD_DIR>/coverage.info`. The `--html` option
   additionally renders the HTML report; drop it if you only need the
   `coverage.info` file (e.g. to upload it). Add `-c` to run each in-VM test
   type in a single VM boot (much faster).

4. Open `<BUILD_DIR>/coverage_html/index.html` in your browser.

`run_all_tests --coverage` takes care of the whole flow: it cleans any stale
coverage data, wires the in-VM runners to dump and merge the kernel's coverage,
captures the host-side unit-test coverage, and merges everything into one
`coverage.info`. For the CI use case, leave out `--html` and upload the
resulting file instead, e.g.:

        <BUILD_DIR>/scripts/generate_kernel_coverage_report --codecov

## Advanced case

In order to get the *full* test coverage, it is necessary to run also Tilck's
interactive tests as well. (The *interactive* tests simulate real user
input (keystrokes on a virtual PS/2 keyboard) and check Tilck's fb console's
output by parsing screenshots.) To run them and get the full test coverage
it's necessary to:

 * Check that you have the [pySerial] python-3 module installed on the system

 * Check that you have [ImageMagick] installed on the system

 * Replace the **step 1** above with:

        <TILCK>/scripts/cmake_run --intr --gcov

That's it: with the interactive build configuration in place, the same
`run_all_tests --coverage` command from step 3 will also run the interactive
tests and fold their coverage into `coverage.info`.

[pySerial]: https://pyserial.readthedocs.io/en/latest/pyserial.html
[ImageMagick]: https://imagemagick.org/
