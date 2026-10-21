/*
    base.cpp

    Unity build entry for base. Including this file compiles all of base into
    the including translation unit; there are no libraries to link.

    A game adds the directory that contains base/ to its include path and
    includes this once, in the single .cpp it compiles:
        #include "base/base.cpp"

    Layers are included bottom up, so a layer may only use the layers
    included before it. types is header-only and comes in through the headers.

    The os layer is split around containers: os memory is what the arena is
    built on, while the rest of os (error messages, and later files, threads
    and windows) needs String8 and arenas. So the order is
        architecture -> os memory -> containers -> os -> log

    For readable String8 / Slice / Arena values in the debugger, also link
    with /NATVIS:path\to\base\base.natvis.

    NOTE: Everything ends up in one translation unit, so every name in base,
    including static helpers, must be unique. Prefix helpers with their layer
    or system (CpuFeatureBit, not Bit).

    TODO: Add math as that layer gets code.
*/

// Layer dependency order, not alphabetical; keep clang-format away from it.
// clang-format off
#include "architecture/architecture.cpp"
#include "os/os_memory.cpp"
#include "containers/containers.cpp"
#include "os/os.cpp"
#include "log/log.cpp"
// clang-format on
