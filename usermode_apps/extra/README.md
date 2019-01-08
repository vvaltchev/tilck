
What is the purpose of this directory
--------------------------------------

This directory has been created in order to facilitate the integration of
projects not part of the Tilck build system with it. If the 3rd party project
follows the rules below, Tilck's build system will automatically compile and
copy project's binaries in Tilck's image. An excellent example project is the
`tfblib`: https://github.com/vvaltchev/tfblib. It can be built independently
as any CMake project but, when a symlink (or a copy) of its root directory
is put here in `extra`, it integrates *smoothly* with Tilck's build system.

Integration rules
-------------------

   1. The project must use CMake
   2. The project must be written in C/C++
   3. The project must produce exclusively statically linked binaries
   4. The project must work on same architecture Tilck is built for
   5. The project must build correctly using a `libmusl` GCC toolchain
   6. The project must be careful with the syscalls used. Many `Linux` syscalls
      are not supported yet on `Tilck`. See the supported syscalls in Tilck's
      wiki: https://github.com/vvaltchev/tilck/wiki.

   7. The project must *not* use the `project()` CMake command when it is being
      compiled in Tilck. In order detect that, use something like:

         ```
         if (NOT "${CMAKE_PROJECT_NAME}" STREQUAL "tilck")

            project(Cool3rdPartyProject C)

         else()

            message(STATUS "Building Cool3rdPartyProject as part of Tilck")

         endif()
         ```

   8. At the end (preferably) of the root `CMakeLists.txt`, the 3rd project have
      to list all the full-path of all the binaries that have to be copied in
      Tilck's image, using the `tilck_add_extra_app` CMake macro. For example:

         ```
         if (${CMAKE_PROJECT_NAME} STREQUAL "tilck")
            tilck_add_extra_app(${CMAKE_CURRENT_BINARY_DIR}/SomeOutputDir/app1)
         endif()
         ```
