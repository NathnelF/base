/*
    base_memory.h

    Byte-level memory helpers used across base in place of the CRT's memcpy,
    memset and memcmp.

    MemCopy, MemSet and MemZero: on MSVC x64 these are rep movsb / rep stosb
    through the __movsb / __stosb intrinsics, which are fast on every CPU
    with ERMSB (Ivy Bridge and later, Zen). Clang and GCC get the compiler
    builtins, which inline small sizes and call the platform memcpy for big
    ones.

    MemCompare returns -1, 0 or 1 (not an arbitrary difference) and
    MemEqual is MemCompare == 0.

    ZII: a zero size is a no-op (or "equal"), and a null pointer never gets
    dereferenced. Comparing a null against a non-null pointer with a nonzero
    size orders the null first.

    NOTE: MemCopy ranges must not overlap. Add a MemMove when something needs
    overlapping copies.

    NOTE: The MSVC MemCompare loop reads u64s from possibly unaligned
    addresses. That is fine on x64 and ARM64, which are the only targets.
*/

#pragma once

#include "../architecture/arch_context.h"
#include "base_types.h"

#if COMPILER_MSVC
#include <intrin.h>
#endif

inline void MemCopy(void *dest, const void *source, u64 size)
{
    if (dest == 0 || source == 0 || size == 0)
    {
        return;
    }
#if COMPILER_MSVC && ARCH_X64
    __movsb((unsigned char *)dest, (const unsigned char *)source, (size_t)size);
#elif COMPILER_MSVC
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)source;
    for (u64 i = 0; i < size; ++i)
    {
        d[i] = s[i];
    }
#else
    __builtin_memcpy(dest, source, size);
#endif
}

inline void MemSet(void *dest, u8 value, u64 size)
{
    if (dest == 0 || size == 0)
    {
        return;
    }
#if COMPILER_MSVC && ARCH_X64
    __stosb((unsigned char *)dest, value, (size_t)size);
#elif COMPILER_MSVC
    u8 *d = (u8 *)dest;
    for (u64 i = 0; i < size; ++i)
    {
        d[i] = value;
    }
#else
    __builtin_memset(dest, value, size);
#endif
}

inline void MemZero(void *dest, u64 size)
{
    MemSet(dest, 0, size);
}

inline i32 MemCompare(const void *a, const void *b, u64 size)
{
    if (size == 0 || a == b)
    {
        return 0;
    }
    if (a == 0)
    {
        return -1;
    }
    if (b == 0)
    {
        return 1;
    }
#if COMPILER_MSVC
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    while (size >= 8)
    {
        if (*(const u64 *)pa != *(const u64 *)pb)
        {
            break;
        }
        pa += 8;
        pb += 8;
        size -= 8;
    }
    while (size > 0)
    {
        if (*pa != *pb)
        {
            if (*pa < *pb)
            {
                return -1;
            }
            return 1;
        }
        ++pa;
        ++pb;
        --size;
    }
    return 0;
#else
    int result = __builtin_memcmp(a, b, size);
    if (result < 0)
    {
        return -1;
    }
    if (result > 0)
    {
        return 1;
    }
    return 0;
#endif
}

inline b32 MemEqual(const void *a, const void *b, u64 size)
{
    return MemCompare(a, b, size) == 0;
}
