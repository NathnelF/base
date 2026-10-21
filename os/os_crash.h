/*
    os_crash.h

    Crash handling: turn any crash into a readable report and a minidump
    instead of a silent exit or an OS error dialog.

    OsCrashHandlerInstall (the log layer calls it at startup unless
    BASE_NO_CRASH_HANDLER is defined) sets up:
    - an unhandled exception filter: access violations, stack overflows,
      divide by zero, illegal instructions, a failed ASSERT_ALWAYS without a
      debugger (it is a breakpoint exception), and FATAL_ERROR.
    - C runtime handlers that would otherwise kill the process without a
      trace: abort(), invalid parameters to CRT functions, pure virtual
      calls. They raise OS_CRASH_CRT_ERROR into the same path.

    On a crash it writes, to stderr and the debugger output:
        *** crash: EXCEPTION_ACCESS_VIOLATION (0xC0000005): write to address 0x0
        *** thread 1234 in C:\path\game.exe
        C:\path\file.cpp(42): CrashingFunction
        C:\path\main.cpp(99): main
        *** minidump: C:\path\game.crash-20261021-153012.dmp
    Frame lines use the file(line) form editors open on double-click. The
    minidump goes next to the executable and opens in Visual Studio or WinDbg
    with the matching .pdb. Then os_crash_hook (if set) gets the report and
    the dump path, e.g. to show a message box, and the process exits with the
    exception code.

    With a debugger attached none of this runs: the debugger stops at the
    crash instead, which is what you want while developing.

    OsCrashRaise sends a message into the same path (FATAL_ERROR uses it).
    OsStackTraceHere formats the calling thread's stack the same way, for
    logging without crashing.

    NOTE: The report is produced on a separate thread with its own stack, so
    a stack overflow can still be reported. SetThreadStackGuarantee leaves
    the crashing thread enough stack to start that thread; Install applies
    it to the calling thread only. TODO: the threading layer applies it to
    every thread it creates.

    NOTE: Symbols come from dbghelp.dll, loaded from System32 on first use,
    so base links no extra library. Without a .pdb, frames print as module
    plus offset.

    TODO: Keep the last few log lines in a ring buffer and include them in
    the report. TODO: POSIX implementation (sigaction + backtrace).
*/

#pragma once

#include "../architecture/arch_context.h"
#include "../containers/arena.h"
#include "../containers/string8.h"
#include "../types/base_types.h"

// Exception codes base raises. Bit 29 set marks them as application codes.
constexpr u32 OS_CRASH_FATAL_ERROR = 0xE0BA5E01;
constexpr u32 OS_CRASH_CRT_ERROR = 0xE0BA5E02;

// Called once after a crash report is written. dump_path is empty if the
// minidump could not be written.
typedef void OsCrashHookFunction(String8 report, String8 dump_path);

inline OsCrashHookFunction *os_crash_hook = 0;

// Safe to call more than once.
void OsCrashHandlerInstall(void);

// Reports message through the crash path and ends the process with code.
[[noreturn]] void OsCrashRaise(u32 code, String8 message);

// The calling thread's stack, one "file(line): function" line per frame,
// skipping skip_frames frames above the caller.
String8 OsStackTraceHere(Arena *arena, u32 skip_frames);
