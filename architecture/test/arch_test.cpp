/*
    arch_test.cpp

    Prints the compile-time target (OS, architecture, compiler, SIMD baseline)
    and the run-time CPU query. Build with `fbuild arch-test-debug` (or
    -release / -profile) from the repo root. The profile config compiles with
    /arch:AVX2, so SIMD_AVX and SIMD_AVX2 should read 1 there and 0 in debug.

    NOTE: Uses printf until the os layer has its own output functions.
*/

#include <stdio.h>

#include "../architecture.cpp"

int main(void)
{
    int bits = 32;
    if (ARCH_64BIT)
    {
        bits = 64;
    }
    const char *endian = "big";
    if (ARCH_LITTLE_ENDIAN)
    {
        endian = "little";
    }

    printf("OS:         %s\n", OperatingSystemName(CURRENT_OS));
    printf("Arch:       %s (%d-bit, %s endian)\n",
           ArchitectureName(CURRENT_ARCH),
           bits,
           endian);
    printf("Compiler:   %s\n", CompilerName(CURRENT_COMPILER));
    printf("Cache line: %d (compile-time)\n", ARCH_CACHE_LINE_SIZE);
    printf(
      "SIMD baseline: SSE2=%d SSE4_2=%d AVX=%d AVX2=%d AVX512=%d NEON=%d\n",
      SIMD_SSE2,
      SIMD_SSE4_2,
      SIMD_AVX,
      SIMD_AVX2,
      SIMD_AVX512,
      SIMD_NEON);

    CpuInfo info = QueryCpuInfo();
    printf("\nCPU arch:   %s\n", ArchitectureName(info.arch));
    printf("Vendor:     %s\n", info.vendor);
    printf("Brand:      %s\n", info.brand);
    printf("Cache line: %u (run-time)\n", info.cache_line_size);
    printf("Features:  ");
    for (u32 i = 0; i < CPU_FEATURE_COUNT; ++i)
    {
        CpuFeature feature = (CpuFeature)(1ull << i);
        if (CpuHasFeature(&info, feature))
        {
            printf(" %s", CpuFeatureName(feature));
        }
    }
    printf("\n");

    return 0;
}
