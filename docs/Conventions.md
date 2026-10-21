This file contains the coding style convetions for this repository.

## Language

- C+ style.
- We compile with C++ and MSCV but only use a select few features of C++.

## Build System

- We use FASTbuild as our build system.
- C:\Scripts\buildf.py has a templated fbuild.bff generator which should serve as the base example for FASTbuild scripts. The root `fbuild.bff` uses its toolchain block.
- Unity build. No libraries.
  - Each layer has a `<layer>/<layer>.cpp` that `#include`s every .cpp in that layer.
  - `base.cpp` (repo root) includes the layer .cpp files in dependency order: architecture -> types -> os memory -> containers -> os -> log, then math. A layer may only use layers included before it. `types` is header-only.
  - The os layer is split around containers: `os/os_memory.cpp` (what the arena is built on) comes before containers, and `os/os.cpp` (everything that needs `String8` or an arena: error messages, files, threads, windows) comes after.
  - Unity files list includes in dependency order, so wrap them in `// clang-format off` / `// clang-format on`; clang-format would otherwise sort them alphabetically.
  - Everything is one translation unit, so every name in base, including `static` helpers, must be unique. Prefix helpers with their layer or system (`CpuFeatureBit`, not `Bit`).
- Tests: each layer test is one .cpp in `<layer>/test/` that includes its layer's .cpp (and any lower layers' .cpp) and has a `main`. Test targets live in the root `fbuild.bff`; run `fbuild debug|release|profile|all` from the repo root.
- Games using base as a submodule add the directory containing `base/` to their include path and `#include "base/base.cpp"` in their own unity file. Base ships no .bff for consumers.
- Within base, include other base files by relative path (`"../types/base_types.h"`) so no include path setup is needed.

## Aesthetics

- Allman style bracing
- See clang-format for more details
- if -> else or switch statements over ternary operators.
- PascalCase for function names, types, and structs.
- SCREAMING_SNAKE_CASE for macros and constexpr
- snake_case for variable names

## Coding Paradigms

- Procedural style organization.
- Limitted use of templates for basic containers and generic typing.
- Operator overloading is okay for accessing, addition and multiplication.
- Create functions rather than constructors.
- Separate functions rather than methods.
- Structs always (no private by default).
- No C++ STL.
- From scratch approach. Use our own functions over libraries and over C standard library when possible.
- Performance is king. Many things will be multithreaded and take advantage of SIMD.
- Favor one long documentation style comment at the top of each file over many individual comment lines.
- Use (TODO: ) and (NOTE: ) to add information for future developers.
- ZII philosophy. Write code that can avoid null checks and still work seamlessly. Use global "nil structs".
- Steal Ryan Fleury and Casey Muratori's ideas they're pretty smart.

## Error Handling and Debugging

Failures never crash and never go silent. Pick the mechanism by what kind of problem it is:

| Situation | Example | Mechanism |
|---|---|---|
| Programmer bug (broken contract) | out-of-bounds index, bad alignment | `ASSERT` / `ASSERT_MSG` (debug only), then fall back to a ZII result so release keeps running. `ASSERT_ALWAYS` only for cheap checks guarding states that are unsafe to continue from. |
| Expected runtime failure | missing file, bad parse, out of memory | Return the ZII value (empty string, nil struct, `b32` false) and `LOG_WARN` the reason, including the OS message (`OsLastErrorMessage`). Callers check only if they care. |
| Caller must branch on the reason | not found vs. access denied | A small reason enum in that API's result struct, next to the ZII value. Only where a caller actually branches. |
| Cannot continue | no window or graphics device | `FATAL_ERROR(fmt, ...)`: logs, writes a crash report with stack trace and minidump, exits. Never returns. |

- No exceptions, no `errno`-style globals, no `Result<T, E>` as a general pattern.
- Lower layers that cannot call the log layer report through a hook (`base_assert_hook`, `arena_failure_hook`) that the log layer installs at startup.
- Log levels: `LOG_TRACE` for noisy detail, `LOG_INFO` for milestones, `LOG_WARN` for recoverable failures, `LOG_ERROR` for failures that lose data or features. `LOG_MIN_LEVEL` compiles out the lower ones (profile builds keep errors only).
- Use `LogScopeBegin` / `LogScopeEnd` to collect the messages of an operation as data (loaders, tools, tests) instead of inventing an error list.
- New types that are hard to read in a debugger get an entry in `base.natvis`.


## Architecture

- This base repo will work as a submodule for other more specific projects. Keep that in mind.
  - Typically games.
- This is essentially an opinionated low level library.
- It will have separate "layers". Each of which is just a folder in this repo 
  - "architecture"
  - "os"
  - "math"
  - "containers"
  - "types"
  - "log"