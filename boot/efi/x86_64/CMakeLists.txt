# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(EFI_ARCH x86_64)
set(GNUEFI_DIR ${TCROOT}/x86_64/gnu-efi)
file(GLOB SOURCES ${GLOB_CONF_DEP} "../*.c" "*.S")


if (${HOST_ARCH} STREQUAL "x86_64")

   # The host arch is x86_64. We're going to the use system compiler for this,
   # instead of one from a pre-built toolchain.

   if (CMAKE_C_COMPILER_ID STREQUAL "GNU")

      file(RELATIVE_PATH relPath ${CMAKE_BINARY_DIR} ${EFI_${EFI_ARCH}_FILE})
      CHECK_C_COMPILER_FLAG(-maccumulate-outgoing-args ACC_OUTGOING_ARGS_SUPP)

      if (ACC_OUTGOING_ARGS_SUPP)
         include (../GenericBuild.cmake)
      else()
         show_no_ms_abi_support_warning(${relPath})
      endif()

   elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")

      # Building the BOOTX64.EFI file with Clang is tricky, in particular using
      # gnu-efi. Even if recent clang versions support MS ABI, the GenericBuild
      # used above doesn't work out-of-the-box. It requires a serious effort.
      # Since Tilck's official toolchain is based on GCC and Clang is partially
      # supported just for static analysis and devel builds, making the EFI
      # bootloaders build with Clang is *not* a must. Until now, this x86_64
      # target just created a fake empty file to make the build pass, after
      # displaying a warning during run of CMake. That's the typical case of
      # devel (contrib) builds (./scripts/cmake_run --contrib).
      #
      # Because this warning is annoying and it would be convenient to just
      # behave as in the GCC case, here a small hack will be used. We'll use
      # (assuming it exists) the system's GCC to build the 64-bit EFI
      # bootloader. That's exactly what happens with the OFFICIAL (default)
      # Tilck builds (just, there CC and CXX and not defined by the user).
      # Because the build host must be a x86_64 linux system and we have to
      # build a 64-bit EFI bootloader, using the system compiler is perfectly
      # fine. Again, even in this case where CC=clang. The CC=clang option has
      # anyway meaning just for the C files in the kernel; the rest of the files
      # and targets will be built anyway with the custom GCC-based toolchain.

      set(CMAKE_C_COMPILER gcc)
      set(CMAKE_CXX_COMPILER g++)
      set(CMAKE_ASM_COMPILER gcc)

      include (../GenericBuild.cmake)

   else()

      message(FATAL_ERROR "Compiler ${CMAKE_C_COMPILER_ID} NOT supported")

   endif()

else()

   set(NAME_PFX_x86_64       gcc_${GCC_TC_VER_}_x86_64)
   set(TC_ROOT_x86_64        ${TCROOT_BA}/${NAME_PFX_x86_64}_musl)
   set(TOOLCHAIN_x86_64      ${TC_ROOT_x86_64}/bin)

   set_cross_compiler_internal(${TOOLCHAIN_x86_64} x86_64)
   include (../GenericBuild.cmake)

endif()



