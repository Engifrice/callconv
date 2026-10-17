/*****************************************************************/ /**
 * @file   platform.h
 * @brief  Internal: selects the calling convention implemented by this build.
 *
 * Exactly one CALLCONV_ABI_* macro is defined to 1. The numeric
 * CALLCONV_ABI_ID must match the CALLCONV_CMAKE_ABI definition passed by
 * CMake, which guarantees that the assembly file selected by the build
 * system matches the target the compiler actually generates code for.
 *********************************************************************/
#ifndef CALLCONV_SRC_PLATFORM_H
#define CALLCONV_SRC_PLATFORM_H

#define CALLCONV_ABI_X86_64_SYSV  0
#define CALLCONV_ABI_X86_64_WIN64 0
#define CALLCONV_ABI_ARM64_AAPCS  0
#define CALLCONV_ABI_ARM64_APPLE  0
#define CALLCONV_ABI_ARM64_WIN    0

#if defined(_M_ARM64EC)
  #error "callconv: ARM64EC is not supported"
#elif defined(_WIN32)
  #if defined(_M_X64) || defined(__x86_64__)
    #undef CALLCONV_ABI_X86_64_WIN64
    #define CALLCONV_ABI_X86_64_WIN64 1
    #define CALLCONV_ABI_ID           2
    #define CALLCONV_ABI_NAME         "x86_64-win64"
  #elif defined(_M_ARM64) || defined(__aarch64__)
    #undef CALLCONV_ABI_ARM64_WIN
    #define CALLCONV_ABI_ARM64_WIN 1
    #define CALLCONV_ABI_ID        5
    #define CALLCONV_ABI_NAME      "arm64-win"
  #else
    #error "callconv: unsupported Windows architecture"
  #endif
#elif defined(__APPLE__)
  #if defined(__aarch64__) || defined(__arm64__)
    #undef CALLCONV_ABI_ARM64_APPLE
    #define CALLCONV_ABI_ARM64_APPLE 1
    #define CALLCONV_ABI_ID          4
    #define CALLCONV_ABI_NAME        "arm64-apple"
  #else
    #error "callconv: unsupported Apple architecture (only arm64 is supported)"
  #endif
#elif defined(__x86_64__)
  #undef CALLCONV_ABI_X86_64_SYSV
  #define CALLCONV_ABI_X86_64_SYSV 1
  #define CALLCONV_ABI_ID          1
  #define CALLCONV_ABI_NAME        "x86_64-sysv"
#elif defined(__aarch64__)
  #undef CALLCONV_ABI_ARM64_AAPCS
  #define CALLCONV_ABI_ARM64_AAPCS 1
  #define CALLCONV_ABI_ID          3
  #define CALLCONV_ABI_NAME        "arm64-aapcs"
#else
  #error "callconv: unsupported platform"
#endif

#if defined(CALLCONV_CMAKE_ABI) && CALLCONV_CMAKE_ABI != CALLCONV_ABI_ID
  #error "callconv: the build system selected assembly for a different calling convention than the compiler targets"
#endif

#if defined(_MSC_VER)
  #include <malloc.h>
  #define CALLCONV_ALLOCA(size) _alloca(size)
#else
  #define CALLCONV_ALLOCA(size) __builtin_alloca(size)
#endif

#if defined(_WIN32)
  #define CALLCONV_HIDDEN
#else
  #define CALLCONV_HIDDEN __attribute__((visibility("hidden")))
#endif

#endif // !CALLCONV_SRC_PLATFORM_H
