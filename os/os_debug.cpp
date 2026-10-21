/*
    os_debug.cpp

    Platform-independent half of os_debug.h. The backend below provides
    everything except OsLastErrorMessage.
*/

#include "os_debug.h"

#include "os_debug_win32.cpp"

#if !OS_WINDOWS
#error "os_debug: no backend for this OS yet"
#endif

String8 OsLastErrorMessage(Arena *arena)
{
    return OsErrorMessage(arena, OsLastErrorCode());
}
