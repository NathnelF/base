This file contains the coding style convetions for this repository.

## Language

- C+ style.
- We compile with C++ and MSCV but only use a select few features of C++.

## Build System

- We use FASTbuild as our build system.
- C:\Scripts\buildf.py has a templated fbuild.bff generator which should serve as the base example for FASTbuild scripts. The root `fbuild.bff` uses its toolchain block.
- Unity build. No libraries.
  - Each layer has a `<layer>/<layer>.cpp` that `#include`s every .cpp in that layer.
  - `base.cpp` (repo root) includes the layer .cpp files in dependency order: architecture -> types -> os -> containers, math. A layer may only use layers included before it. `types` is header-only.
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