/*
    arch_cpu.cpp

    Implementation of QueryCpuInfo() and friends from arch_cpu.h.

    x86/x64 reads CPUID leaves 0 (vendor, max leaf), 1 (SSE..AVX, OSXSAVE,
    CLFLUSH line size), 7/0 (AVX2, BMI, AVX-512), 0x80000001 (LZCNT),
    0x80000002-4 (brand string) and 0x80000006 (cache line fallback).
    VEX-encoded features (AVX, F16C, FMA, AVX2) are only reported when OSXSAVE
    is set and XCR0 has the SSE and AVX state bits (1-2). AVX-512 additionally
    needs the opmask/ZMM state bits (5-7). Without these checks a CPU that
    supports AVX under an OS that does not save YMM registers would crash on
    first use.

    MSVC uses __cpuidex/_xgetbv from <intrin.h>. GCC and Clang use
    __cpuid_count from <cpuid.h> and inline asm for XGETBV, since the
    _xgetbv intrinsic there requires compiling with -mxsave.
*/

#include "arch_cpu.h"

#if ARCH_X86 || ARCH_X64
#if COMPILER_MSVC
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#if ARCH_X86 || ARCH_X64

static void CpuId(u32 leaf, u32 subleaf, u32 *regs)
{
#if COMPILER_MSVC
    int r[4];
    __cpuidex(r, (int)leaf, (int)subleaf);
    regs[0] = (u32)r[0];
    regs[1] = (u32)r[1];
    regs[2] = (u32)r[2];
    regs[3] = (u32)r[3];
#else
    __cpuid_count(leaf, subleaf, regs[0], regs[1], regs[2], regs[3]);
#endif
}

static u64 XGetBv(u32 index)
{
#if COMPILER_MSVC
    return _xgetbv(index);
#else
    u32 eax;
    u32 edx;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((u64)edx << 32) | eax;
#endif
}

static void CpuCopyRegisterBytes(char *dest, u32 reg)
{
    dest[0] = (char)(reg & 0xFF);
    dest[1] = (char)((reg >> 8) & 0xFF);
    dest[2] = (char)((reg >> 16) & 0xFF);
    dest[3] = (char)((reg >> 24) & 0xFF);
}

static u64 CpuFeatureBit(u32 reg, u32 bit, CpuFeature feature)
{
    if ((reg >> bit) & 1)
    {
        return feature;
    }
    return 0;
}

#endif

CpuInfo QueryCpuInfo(void)
{
    CpuInfo info;
    info.arch = CURRENT_ARCH;
    info.vendor[0] = 0;
    info.brand[0] = 0;
    info.features = 0;
    info.cache_line_size = 0;

#if ARCH_X86 || ARCH_X64
    u32 regs[4];

    CpuId(0, 0, regs);
    u32 max_leaf = regs[0];
    CpuCopyRegisterBytes(info.vendor + 0, regs[1]);
    CpuCopyRegisterBytes(info.vendor + 4, regs[3]);
    CpuCopyRegisterBytes(info.vendor + 8, regs[2]);
    info.vendor[12] = 0;

    CpuId(0x80000000, 0, regs);
    u32 max_ext_leaf = regs[0];

    b32 os_avx = 0;
    b32 os_avx512 = 0;

    if (max_leaf >= 1)
    {
        CpuId(1, 0, regs);
        u32 ebx = regs[1];
        u32 ecx = regs[2];
        u32 edx = regs[3];

        info.features |= CpuFeatureBit(edx, 25, CpuFeature_SSE);
        info.features |= CpuFeatureBit(edx, 26, CpuFeature_SSE2);
        info.features |= CpuFeatureBit(ecx, 0, CpuFeature_SSE3);
        info.features |= CpuFeatureBit(ecx, 9, CpuFeature_SSSE3);
        info.features |= CpuFeatureBit(ecx, 19, CpuFeature_SSE4_1);
        info.features |= CpuFeatureBit(ecx, 20, CpuFeature_SSE4_2);
        info.features |= CpuFeatureBit(ecx, 23, CpuFeature_POPCNT);

        b32 osxsave = (ecx >> 27) & 1;
        if (osxsave)
        {
            u64 xcr0 = XGetBv(0);
            os_avx = (xcr0 & 0x6) == 0x6;
            os_avx512 = os_avx && (xcr0 & 0xE0) == 0xE0;
        }

        if (os_avx)
        {
            info.features |= CpuFeatureBit(ecx, 28, CpuFeature_AVX);
            info.features |= CpuFeatureBit(ecx, 29, CpuFeature_F16C);
            info.features |= CpuFeatureBit(ecx, 12, CpuFeature_FMA);
        }

        // NOTE: CLFLUSH line size, in 8 byte units. Valid when CLFSH (edx bit
        // 19) is set.
        if ((edx >> 19) & 1)
        {
            info.cache_line_size = ((ebx >> 8) & 0xFF) * 8;
        }
    }

    if (max_leaf >= 7)
    {
        CpuId(7, 0, regs);
        u32 ebx = regs[1];

        info.features |= CpuFeatureBit(ebx, 3, CpuFeature_BMI1);
        info.features |= CpuFeatureBit(ebx, 8, CpuFeature_BMI2);

        if (os_avx)
        {
            info.features |= CpuFeatureBit(ebx, 5, CpuFeature_AVX2);
        }

        if (os_avx512)
        {
            info.features |= CpuFeatureBit(ebx, 16, CpuFeature_AVX512F);
            info.features |= CpuFeatureBit(ebx, 17, CpuFeature_AVX512DQ);
            info.features |= CpuFeatureBit(ebx, 28, CpuFeature_AVX512CD);
            info.features |= CpuFeatureBit(ebx, 30, CpuFeature_AVX512BW);
            info.features |= CpuFeatureBit(ebx, 31, CpuFeature_AVX512VL);
        }
    }

    if (max_ext_leaf >= 0x80000001)
    {
        CpuId(0x80000001, 0, regs);
        info.features |= CpuFeatureBit(regs[2], 5, CpuFeature_LZCNT);
    }

    if (max_ext_leaf >= 0x80000004)
    {
        for (u32 i = 0; i < 3; ++i)
        {
            CpuId(0x80000002 + i, 0, regs);
            for (u32 r = 0; r < 4; ++r)
            {
                CpuCopyRegisterBytes(info.brand + i * 16 + r * 4, regs[r]);
            }
        }
        info.brand[48] = 0;

        // NOTE: Intel right-aligns the brand string with leading spaces.
        u32 start = 0;
        while (info.brand[start] == ' ')
        {
            ++start;
        }
        if (start > 0)
        {
            u32 i = 0;
            while (info.brand[start + i])
            {
                info.brand[i] = info.brand[start + i];
                ++i;
            }
            info.brand[i] = 0;
        }
    }

    if (info.cache_line_size == 0 && max_ext_leaf >= 0x80000006)
    {
        CpuId(0x80000006, 0, regs);
        info.cache_line_size = regs[2] & 0xFF;
    }
#elif ARCH_ARM64
    info.features |= CpuFeature_NEON;
#elif ARCH_ARM32
    if (SIMD_NEON)
    {
        info.features |= CpuFeature_NEON;
    }
    // TODO: ARM32 run-time NEON detection without compile-time NEON needs the
    // os layer.
#endif

    if (info.cache_line_size == 0)
    {
        info.cache_line_size = ARCH_CACHE_LINE_SIZE;
    }

    return info;
}

b32 CpuHasFeature(CpuInfo *info, CpuFeature feature)
{
    return (info->features & feature) == feature;
}

const char *CpuFeatureName(CpuFeature feature)
{
    switch (feature)
    {
        case CpuFeature_SSE:
            return "SSE";
        case CpuFeature_SSE2:
            return "SSE2";
        case CpuFeature_SSE3:
            return "SSE3";
        case CpuFeature_SSSE3:
            return "SSSE3";
        case CpuFeature_SSE4_1:
            return "SSE4.1";
        case CpuFeature_SSE4_2:
            return "SSE4.2";
        case CpuFeature_POPCNT:
            return "POPCNT";
        case CpuFeature_AVX:
            return "AVX";
        case CpuFeature_F16C:
            return "F16C";
        case CpuFeature_FMA:
            return "FMA";
        case CpuFeature_AVX2:
            return "AVX2";
        case CpuFeature_BMI1:
            return "BMI1";
        case CpuFeature_BMI2:
            return "BMI2";
        case CpuFeature_LZCNT:
            return "LZCNT";
        case CpuFeature_AVX512F:
            return "AVX512F";
        case CpuFeature_AVX512DQ:
            return "AVX512DQ";
        case CpuFeature_AVX512BW:
            return "AVX512BW";
        case CpuFeature_AVX512VL:
            return "AVX512VL";
        case CpuFeature_AVX512CD:
            return "AVX512CD";
        case CpuFeature_NEON:
            return "NEON";
        default:
            return "Unknown";
    }
}
