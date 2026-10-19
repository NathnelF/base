/*
    base.cpp

    Unity build entry for base. Including this file compiles all of base into
    the including translation unit; there are no libraries to link.

    A game adds the directory that contains base/ to its include path and
    includes this once, in the single .cpp it compiles:
        #include "base/base.cpp"

    Layers are included bottom up, so a layer may only use the layers
    included before it. types is header-only and comes in through the headers.

    NOTE: Everything ends up in one translation unit, so every name in base,
    including static helpers, must be unique. Prefix helpers with their layer
    or system (CpuFeatureBit, not Bit).

    TODO: Add containers and math as those layers get code.
*/

#include "architecture/architecture.cpp"
#include "os/os.cpp"
