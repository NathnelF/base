/*
    log.h

    Logging, log scopes, and the reporting hooks for asserts and arena
    failures. This is how ZII failures stop being silent: a function that
    fails returns its zero value AND logs why, so callers only check results
    they care about and nothing goes unexplained.

    LOG_TRACE / LOG_INFO / LOG_WARN / LOG_ERROR(fmt, ...) take Str8PushF
    format strings (so %S prints a String8). Each message becomes one line:
        C:\path\file.cpp(123): warning: message
    which Visual Studio, 10x and most editors open on double-click.

    Levels below LOG_MIN_LEVEL compile out entirely (if constexpr), but the
    arguments are still type-checked. Defaults: everything in debug, Warning
    and up with NDEBUG. Define LOG_MIN_LEVEL before including base to
    override; the profile config builds with LOG_MIN_LEVEL=3 (errors only).

    Output goes to the debugger (OutputDebugString, only when one is
    attached) and to stderr. LogSetConsoleOutput(0) mutes stderr, e.g. for
    tests that expect failures.

    Log scopes capture messages for code that wants them as data, such as a
    loader that shows the user what went wrong, or a test:
        LogScopeBegin(arena);
        LoadLevel(...);
        String8List problems = LogScopeEnd();
    Every message logged in between (on this thread) is copied into arena and
    also printed as usual. Scopes nest; a message goes to the innermost one.
    LogScopeEnd without a matching begin returns an empty list.

    Hooks: at program start this layer installs base_assert_hook (prints
    "file(line): assert: expression: message", then breaks),
    arena_failure_hook (a warning for a failed ArenaAlloc, and for the first
    failed push of each arena) and os_mem_failure_hook (a warning naming the
    failed reserve / commit / decommit / release with the OS's reason). It
   leaves alone any hook already set. It also installs the crash handler
   (os_crash.h) unless BASE_NO_CRASH_HANDLER is defined.

    FATAL_ERROR(fmt, ...) is for states the program cannot continue from
    (no window, no graphics device). It logs "file(line): fatal: message",
    then goes through the crash handler (stack trace, minidump) and exits.
    It never returns.

    NOTE: Formatting uses a per-thread scratch arena that is reset after
    every message, so logging costs no lasting memory. TODO: release the
    scratch arena at thread exit once the threading layer exists.

    NOTE: The scope's arena must outlive the scope: do not pop it past the
    LogScopeBegin before calling LogScopeEnd.

    NOTE: A message logged while another is being written on the same thread
    (for example from inside a hook) is dropped instead of recursing.
*/

#pragma once

#include <stdarg.h>

#include "../containers/arena.h"
#include "../containers/string8.h"
#include "../types/base_assert.h"
#include "../types/base_types.h"

enum LogLevel
{
    LogLevel_Trace = 0,
    LogLevel_Info = 1,
    LogLevel_Warning = 2,
    LogLevel_Error = 3,
    LogLevel_Count,
};

#ifndef LOG_MIN_LEVEL
#if defined(NDEBUG)
#define LOG_MIN_LEVEL 2
#else
#define LOG_MIN_LEVEL 0
#endif
#endif

#define LOG_AT(level, ...)                                                     \
    do                                                                         \
    {                                                                          \
        if constexpr ((level) >= LOG_MIN_LEVEL)                                \
        {                                                                      \
            LogMessage((level), __FILE__, __LINE__, __VA_ARGS__);              \
        }                                                                      \
    } while (0)

#define LOG_TRACE(...) LOG_AT(LogLevel_Trace, __VA_ARGS__)
#define LOG_INFO(...) LOG_AT(LogLevel_Info, __VA_ARGS__)
#define LOG_WARN(...) LOG_AT(LogLevel_Warning, __VA_ARGS__)
#define LOG_ERROR(...) LOG_AT(LogLevel_Error, __VA_ARGS__)

// Prefer the macros; these skip the compile-time level filter.
void LogMessage(LogLevel level,
                const char *file,
                int line,
                const char *fmt,
                ...);
void LogMessageV(LogLevel level,
                 const char *file,
                 int line,
                 const char *fmt,
                 va_list args);

const char *LogLevelName(LogLevel level);

void LogScopeBegin(Arena *arena);
String8List LogScopeEnd(void);

void LogSetConsoleOutput(b32 enabled);

#define FATAL_ERROR(...) FatalError(__FILE__, __LINE__, __VA_ARGS__)

[[noreturn]] void FatalError(const char *file, int line, const char *fmt, ...);
