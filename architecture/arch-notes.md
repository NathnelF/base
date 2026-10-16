Architecture layer: what OS, compiler and CPU the code is built for, and what the running CPU supports.

- `arch_context.h` - compile-time OS / compiler / arch / SIMD-baseline macros (always 0 or 1), enums, `CURRENT_*` constants. No includes.
- `arch_cpu.h`, `arch_cpu.cpp` - `QueryCpuInfo()` run-time CPUID query (vendor, brand, cache line, feature bits gated on OS register support).
- `arch.h` - umbrella include for the declarations.
- `architecture.cpp` - the layer's unity file, included by `base.cpp`.
- `test/` - `arch_test.exe` printing all of the above. `fbuild arch-test-debug` from the repo root.
