/*
    os_lower.cpp

    Unity file for the lower part of the os layer: what containers are built
    on, so it is included before them. Currently just memory (the arena
    reserves and commits through it). Anything here may not use String8 or
    Arena.

    NOTE: The os layer is split around containers; see os_upper.cpp.
*/

// clang-format off
#include "os_memory.cpp"
// clang-format on
