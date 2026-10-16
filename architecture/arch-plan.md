# Architecture layer plan

## Context
`base` is a from-scratch C-style C++ library used as a submodule by games. The `architecture/` layer is empty. It needs to tell code what OS, compiler, and CPU architecture it is built for (compile time), and which SIMD extensions the running CPU actually has (run time), so higher layers can pick code paths for SIMD and size things for multithreading. Conventions: `docs/Conventions.md` (Allman, PascalCase types/functions, SCREAMING_SNAKE macros/constexpr, snake_case vars, no STL, minimal CRT, one doc comment at top of each file, TODO:/NOTE:).

## Layout feedback (for the reply, not code)
- Submodule: agree. Consumers add the parent dir to the include path and write `#include "base/architecture/arch_context.h"`.
- Dependency order should be `architecture -> types -> os -> containers/math`. Compile-time detection must be the bottom layer with zero includes, so `types` can later use it (e.g. drop `<stdint.h>` and define types from compiler knowledge).
- `CLAUDE.md` points to `docs/Convetions.md`; the file is `docs/Conventions.md`.
- Core/thread count needs OS calls (`GetLogicalProcessorInformationEx`, `sysconf`, `sysctl`), so it belongs in `os/`, not here. Same for ARM runtime feature queries.

## Files
### `architecture/arch_context.h` — compile-time, no includes
- Macros, always defined to 0 or 1 (so `#if` and plain `if` both work, and typos are caught):
  - OS: `OS_WINDOWS`, `OS_LINUX`, `OS_MAC`, `OS_IOS`, `OS_ANDROID` (from `_WIN32`, `__linux__`, `__APPLE__` + `TargetConditionals`-free `__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__`, `__ANDROID__`).
  - Compiler: `COMPILER_MSVC`, `COMPILER_CLANG`, `COMPILER_GCC` (check clang before MSVC, since clang-cl defines `_MSC_VER`).
  - Arch: `ARCH_X86`, `ARCH_X64`, `ARCH_ARM32`, `ARCH_ARM64`; derived `ARCH_64BIT`, `ARCH_32BIT`, `ARCH_LITTLE_ENDIAN`.
  - Compile-time SIMD baseline (what the compiler is allowed to emit): `SIMD_SSE2` (always on x64), `SIMD_SSE4_2`, `SIMD_AVX`, `SIMD_AVX2`, `SIMD_AVX512`, `SIMD_NEON` (always on ARM64). Uses `__AVX2__` etc.; MSVC doesn't define SSE3–4.2 macros, so `__AVX__` implies them.
  - `#error` when no OS / arch / compiler matched.
- Constants: `ARCH_CACHE_LINE_SIZE` (64; 128 on Apple ARM64) for padding shared data in multithreaded code.
- Enums (PascalCase type, `Type_Value` members, `_Count` sentinel):
  - `OperatingSystem { OperatingSystem_Null, _Windows, _Linux, _Mac, _IOS, _Android, _Count }`
  - `Architecture { Architecture_Null, _X86, _X64, _ARM32, _ARM64, _Count }`
  - `Compiler { Compiler_Null, _MSVC, _Clang, _GCC, _Count }`
- `constexpr` `CURRENT_OS`, `CURRENT_ARCH`, `CURRENT_COMPILER` resolved from the macros.
- `OperatingSystemName()`, `ArchitectureName()`, `CompilerName()` returning `const char *` via switch.

### `architecture/arch_cpu.h` + `arch_cpu.cpp` — run-time CPU query
- Includes `arch_context.h` and `types/base_types.h`.
- `enum CpuFeature : u64` bit flags: SSE, SSE2, SSE3, SSSE3, SSE4_1, SSE4_2, POPCNT, AVX, F16C, FMA, AVX2, BMI1, BMI2, LZCNT, AVX512F, AVX512DQ, AVX512BW, AVX512VL, AVX512CD, NEON.
- `struct CpuInfo { Architecture arch; char vendor[13]; char brand[49]; u64 features; u32 cache_line_size; }`
- `CpuInfo QueryCpuInfo(void);` `b32 CpuHasFeature(CpuInfo *info, CpuFeature feature);` `const char *CpuFeatureName(CpuFeature);`
- x86/x64: `__cpuid`/`__cpuidex`/`_xgetbv` from `<intrin.h>` (MSVC) or `<cpuid.h>` (GCC/Clang). Leaves 0, 1, 7, 0x80000001, 0x80000002–4 (brand), 0x80000006/leaf 1 (cache line). AVX/AVX2/FMA only reported if OSXSAVE is set and XCR0 bits 1–2 are set; AVX-512 additionally needs XCR0 bits 5–7. Without this, the OS-not-saving-YMM case crashes on use.
- ARM64: report NEON unconditionally. NOTE/TODO that finer features (dotprod, SVE) need OS queries in `os/`.
- Only compiler intrinsic headers; no CRT.

### `architecture/arch.h`
Umbrella include for both headers. `architecture/architecture.cpp` is the layer's unity file (includes `arch_cpu.cpp`); `base.cpp` includes it (unity build; no globals).

### `architecture/test/arch_test.cpp`
- Prints OS, arch, compiler, compile-time SIMD baseline, then the `QueryCpuInfo` result. Uses `printf` with a NOTE that it moves to the os layer's output once that exists.
- Includes `../architecture.cpp`; built by the `arch-test-<config>` targets in the root `fbuild.bff` (toolchain block from `C:\Scripts\buildf.py`). See `docs/Conventions.md`.
- Replace `architecture/arch-notes.md` content with a short description of the files (or delete it).

## Verification
1. `fbuild debug` and `fbuild profile` from the repo root (profile has `/arch:AVX2`, so `SIMD_AVX2` should flip to 1).
2. Run the exe: expect Windows / x64 / MSVC, and features matching this machine's CPU (cross-check with CPU-Z or `coreinfo`).
3. Compile with `/W4` cleanly; `clang-format` the new files with the repo `.clang-format`.
