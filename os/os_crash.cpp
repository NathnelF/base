/*
    os_crash.cpp

    Platform-independent half of os_crash.h; everything currently lives in
    the backend.
*/

#include "os_crash.h"
#include "os_debug.h"

#include "os_crash_win32.cpp"

#if !OS_WINDOWS
#error "os_crash: no backend for this OS yet"
#endif
