/*
    arch_cpu.h

    Run-time query of the CPU the program is actually running on. Use this to
    pick SIMD code paths beyond the compile-time baseline in arch_context.h.

    Call QueryCpuInfo() once at startup and keep the result; it executes
    CPUID several times and is not meant for hot paths. `features` is a mask of
    CpuFeature bits. A feature is only reported when both the CPU and the OS
    support it (AVX and AVX-512 need the OS to save the wider registers on a
    context switch, which is checked through XGETBV).

    Implemented in arch_cpu.cpp, pulled into the unity build through
    architecture.cpp. It uses compiler intrinsics only, no CRT and no globals.

    NOTE: On ARM64 NEON is reported unconditionally since it is part of the
    base ISA. vendor and brand are left empty.

    TODO: Finer ARM features (dotprod, SVE, crypto) need OS queries
    (IsProcessorFeaturePresent, getauxval, sysctlbyname) and belong in the
    os layer.

    TODO: Core and thread counts also need OS calls and belong in the os layer.
*/

#pragma once

#include "../types/base_types.h"
#include "arch_context.h"

enum CpuFeature : u64
{
    CpuFeature_SSE = 1ull << 0,
    CpuFeature_SSE2 = 1ull << 1,
    CpuFeature_SSE3 = 1ull << 2,
    CpuFeature_SSSE3 = 1ull << 3,
    CpuFeature_SSE4_1 = 1ull << 4,
    CpuFeature_SSE4_2 = 1ull << 5,
    CpuFeature_POPCNT = 1ull << 6,
    CpuFeature_AVX = 1ull << 7,
    CpuFeature_F16C = 1ull << 8,
    CpuFeature_FMA = 1ull << 9,
    CpuFeature_AVX2 = 1ull << 10,
    CpuFeature_BMI1 = 1ull << 11,
    CpuFeature_BMI2 = 1ull << 12,
    CpuFeature_LZCNT = 1ull << 13,
    CpuFeature_AVX512F = 1ull << 14,
    CpuFeature_AVX512DQ = 1ull << 15,
    CpuFeature_AVX512BW = 1ull << 16,
    CpuFeature_AVX512VL = 1ull << 17,
    CpuFeature_AVX512CD = 1ull << 18,
    CpuFeature_NEON = 1ull << 19,
};

constexpr u32 CPU_FEATURE_COUNT = 20;

struct CpuInfo
{
    Architecture arch;
    char vendor[13];
    char brand[49];
    u64 features;
    u32 cache_line_size;
};

CpuInfo QueryCpuInfo(void);
b32 CpuHasFeature(CpuInfo *info, CpuFeature feature);
const char *CpuFeatureName(CpuFeature feature);
