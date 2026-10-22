/*
    os_debug.h

    OS error messages and raw diagnostic output, for the log layer and for
    any code that wants to explain an OS failure.

    OsErrorMessage turns an OS error code into the system's own text, e.g.
    "The system cannot find the file specified." (trailing newline removed),
    allocated in the arena. Unknown codes give "OS error 1234 (0x000004D2)".
    OsLastErrorMessage does the same for the calling thread's last error, so
    call it right after the failing OS call.

    OsDebuggerWrite takes UTF-8 and does no allocation: it converts to
    UTF-16 in a stack buffer, chunk by chunk. The standard streams (stderr,
    stdout, stdin) are in os_console.h.

    This file is part of the upper os layer: it needs String8 and Arena, so
    it is included after containers (see base.cpp).

    TODO: POSIX implementation (errno + strerror_r, and a ptrace /
    sysctl check for the debugger).
*/

#pragma once

#include "../architecture/arch_context.h"
#include "../containers/arena.h"
#include "../containers/string8.h"
#include "../types/base_types.h"

u32 OsLastErrorCode(void);
String8 OsErrorMessage(Arena *arena, u32 code);
String8 OsLastErrorMessage(Arena *arena);

b32 OsDebuggerPresent(void);
void OsDebuggerWrite(String8 text);
