/*
    os.cpp

    Unity file for the upper part of the os layer: everything that needs
    containers (String8, Arena). The per-OS backends come in through the file
    that uses them.

    NOTE: The os layer is split around containers. os_memory.cpp is the
    lower part (the arena is built on it) and is included on its own, before
    containers; this file comes after containers. base.cpp includes them in
    that order, and so do the tests:
        architecture -> os/os_memory.cpp -> containers -> os/os.cpp -> log
*/

// clang-format off
#include "os_debug.cpp"
#include "os_file.cpp"
#include "os_crash.cpp"
// clang-format on
