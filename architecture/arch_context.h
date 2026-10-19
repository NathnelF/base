/*
    arch_context.h

    Compile-time description of the target this translation unit is built
    for: operating system, compiler, CPU architecture, and the SIMD baseline
    the compiler is allowed to emit. This is the bottom of the dependency
    graph and must stay free of includes so every other layer (including
    types) can use it.

    Every macro below is always defined to 0 or 1. Test with `#if OS_WINDOWS`
    or `if (OS_WINDOWS)`, never `#ifdef`. With -Wundef / C4668 a misspelled
    macro is a warning instead of a silent 0.

    OS:        OS_WINDOWS, OS_LINUX, OS_MAC, OS_IOS, OS_ANDROID
    Compiler:  COMPILER_MSVC, COMPILER_CLANG, COMPILER_GCC
    Arch:      ARCH_X86, ARCH_X64, ARCH_ARM32, ARCH_ARM64,
               ARCH_64BIT, ARCH_32BIT, ARCH_LITTLE_ENDIAN
    SIMD:      SIMD_SSE2, SIMD_SSE4_2, SIMD_AVX, SIMD_AVX2,
               SIMD_AVX512, SIMD_NEON

    The SIMD macros describe the compile-time baseline only (/arch:AVX2,
    -mavx2, ...). Whether the running CPU supports more is a run-time
    question answered by QueryCpuInfo() in arch_cpu.h.

    base is 64-bit only; a 32-bit target is a compile error. Every current
    console and mobile platform is 64-bit, and the memory code assumes
    sizes up to u64 fit in size_t.

    ARCH_CACHE_LINE_SIZE is the line size to pad shared data to in
    multithreaded code so independent writers do not false-share.

    NOTE: clang is checked before MSVC because clang-cl also defines
    _MSC_VER. COMPILER_CLANG is 1 for clang-cl; check _MSC_VER directly if
    the MSVC front-end flavour matters.

    NOTE: Android is checked before Linux because Android also defines
    __linux__.

    NOTE: MSVC defines no macros for SSE3 through SSE4.2. __AVX__ implies
    them, so SIMD_SSE4_2 is 1 whenever SIMD_AVX is.

    TODO: ARM64EC defines _M_X64 and is treated as x64 here. Revisit if we
    ever target it.

    TODO: Verify the iOS detection on an Apple toolchain. Older clang
    defines __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__, newer clang
    defines __ENVIRONMENT_IOS__.
*/

#pragma once

#if defined(_WIN32)
#define OS_WINDOWS 1
#elif defined(__ANDROID__)
#define OS_ANDROID 1
#elif defined(__linux__)
#define OS_LINUX 1
#elif defined(__APPLE__)
#if defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) ||                 \
  defined(__ENVIRONMENT_IOS__)
#define OS_IOS 1
#else
#define OS_MAC 1
#endif
#else
#error "arch_context.h: unknown operating system"
#endif

#ifndef OS_WINDOWS
#define OS_WINDOWS 0
#endif
#ifndef OS_LINUX
#define OS_LINUX 0
#endif
#ifndef OS_MAC
#define OS_MAC 0
#endif
#ifndef OS_IOS
#define OS_IOS 0
#endif
#ifndef OS_ANDROID
#define OS_ANDROID 0
#endif

#if defined(__clang__)
#define COMPILER_CLANG 1
#elif defined(_MSC_VER)
#define COMPILER_MSVC 1
#elif defined(__GNUC__)
#define COMPILER_GCC 1
#else
#error "arch_context.h: unknown compiler"
#endif

#ifndef COMPILER_MSVC
#define COMPILER_MSVC 0
#endif
#ifndef COMPILER_CLANG
#define COMPILER_CLANG 0
#endif
#ifndef COMPILER_GCC
#define COMPILER_GCC 0
#endif

#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
#define ARCH_X64 1
#elif defined(_M_IX86) || defined(__i386__)
#define ARCH_X86 1
#elif defined(_M_ARM64) || defined(__aarch64__)
#define ARCH_ARM64 1
#elif defined(_M_ARM) || defined(__arm__)
#define ARCH_ARM32 1
#else
#error "arch_context.h: unknown architecture"
#endif

#ifndef ARCH_X86
#define ARCH_X86 0
#endif
#ifndef ARCH_X64
#define ARCH_X64 0
#endif
#ifndef ARCH_ARM32
#define ARCH_ARM32 0
#endif
#ifndef ARCH_ARM64
#define ARCH_ARM64 0
#endif

#if ARCH_X64 || ARCH_ARM64
#define ARCH_64BIT 1
#define ARCH_32BIT 0
#else
#define ARCH_64BIT 0
#define ARCH_32BIT 1
#endif

#if !ARCH_64BIT
#error "base is 64-bit only"
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) &&                \
  (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define ARCH_LITTLE_ENDIAN 0
#else
#define ARCH_LITTLE_ENDIAN 1
#endif

#if ARCH_X64 || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define SIMD_SSE2 1
#else
#define SIMD_SSE2 0
#endif

#if defined(__SSE4_2__) || defined(__AVX__)
#define SIMD_SSE4_2 1
#else
#define SIMD_SSE4_2 0
#endif

#if defined(__AVX__)
#define SIMD_AVX 1
#else
#define SIMD_AVX 0
#endif

#if defined(__AVX2__)
#define SIMD_AVX2 1
#else
#define SIMD_AVX2 0
#endif

#if defined(__AVX512F__)
#define SIMD_AVX512 1
#else
#define SIMD_AVX512 0
#endif

#if ARCH_ARM64 || defined(__ARM_NEON) || defined(__ARM_NEON__)
#define SIMD_NEON 1
#else
#define SIMD_NEON 0
#endif

#if ARCH_ARM64 && (OS_MAC || OS_IOS)
#define ARCH_CACHE_LINE_SIZE 128
#else
#define ARCH_CACHE_LINE_SIZE 64
#endif

enum OperatingSystem
{
    OperatingSystem_Null,
    OperatingSystem_Windows,
    OperatingSystem_Linux,
    OperatingSystem_Mac,
    OperatingSystem_IOS,
    OperatingSystem_Android,
    OperatingSystem_Count
};

enum Architecture
{
    Architecture_Null,
    Architecture_X86,
    Architecture_X64,
    Architecture_ARM32,
    Architecture_ARM64,
    Architecture_Count
};

enum Compiler
{
    Compiler_Null,
    Compiler_MSVC,
    Compiler_Clang,
    Compiler_GCC,
    Compiler_Count
};

#if OS_WINDOWS
constexpr OperatingSystem CURRENT_OS = OperatingSystem_Windows;
#elif OS_LINUX
constexpr OperatingSystem CURRENT_OS = OperatingSystem_Linux;
#elif OS_MAC
constexpr OperatingSystem CURRENT_OS = OperatingSystem_Mac;
#elif OS_IOS
constexpr OperatingSystem CURRENT_OS = OperatingSystem_IOS;
#elif OS_ANDROID
constexpr OperatingSystem CURRENT_OS = OperatingSystem_Android;
#endif

#if ARCH_X86
constexpr Architecture CURRENT_ARCH = Architecture_X86;
#elif ARCH_X64
constexpr Architecture CURRENT_ARCH = Architecture_X64;
#elif ARCH_ARM32
constexpr Architecture CURRENT_ARCH = Architecture_ARM32;
#elif ARCH_ARM64
constexpr Architecture CURRENT_ARCH = Architecture_ARM64;
#endif

#if COMPILER_MSVC
constexpr Compiler CURRENT_COMPILER = Compiler_MSVC;
#elif COMPILER_CLANG
constexpr Compiler CURRENT_COMPILER = Compiler_Clang;
#elif COMPILER_GCC
constexpr Compiler CURRENT_COMPILER = Compiler_GCC;
#endif

inline const char *OperatingSystemName(OperatingSystem os)
{
    switch (os)
    {
        case OperatingSystem_Windows:
            return "Windows";
        case OperatingSystem_Linux:
            return "Linux";
        case OperatingSystem_Mac:
            return "Mac";
        case OperatingSystem_IOS:
            return "iOS";
        case OperatingSystem_Android:
            return "Android";
        default:
            return "Unknown";
    }
}

inline const char *ArchitectureName(Architecture arch)
{
    switch (arch)
    {
        case Architecture_X86:
            return "x86";
        case Architecture_X64:
            return "x64";
        case Architecture_ARM32:
            return "ARM32";
        case Architecture_ARM64:
            return "ARM64";
        default:
            return "Unknown";
    }
}

inline const char *CompilerName(Compiler compiler)
{
    switch (compiler)
    {
        case Compiler_MSVC:
            return "MSVC";
        case Compiler_Clang:
            return "Clang";
        case Compiler_GCC:
            return "GCC";
        default:
            return "Unknown";
    }
}
