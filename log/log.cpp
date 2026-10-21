/*
    log.cpp

    Implementation of log.h, and the log layer's unity file.

    Per-thread state (thread_local, zero-initialized, so no init call):
    - scratch: an arena created on first use. Each message is formatted into
      it inside an ArenaTemp, so it never grows past the longest message.
    - top_scope: the innermost LogScope, a node pushed into the caller's
      arena by LogScopeBegin.
    - dropped_scopes / LogScope::dropped_children: LogScopeBegin can fail
      (nil arena). It then records a dropped scope on whatever scope is on
      top, so the matching LogScopeEnd returns an empty list and every other
      begin / end still pairs up correctly.
    - in_log: guards against recursion, e.g. the arena failure hook firing
      while the log's own scratch push fails.

    The hooks are installed by a static initializer, which runs before main
    in the including translation unit.
*/

#include "log.h"
#include "../os/os_crash.h"
#include "../os/os_debug.h"
#include "../os/os_file.h"
#include "../os/os_memory.h"

constexpr u64 LOG_SCRATCH_RESERVE = 1ull << 30;

struct LogScope
{
    LogScope *parent;
    Arena *arena;
    String8List messages;
    u64 dropped_children;
};

struct LogThreadState
{
    Arena *scratch;
    LogScope *top_scope;
    u64 dropped_scopes;
    b32 in_log;
};

static thread_local LogThreadState log_thread;
static b32 log_console_enabled = 1;

const char *LogLevelName(LogLevel level)
{
    switch (level)
    {
        case LogLevel_Trace:
            return "trace";
        case LogLevel_Info:
            return "info";
        case LogLevel_Warning:
            return "warning";
        case LogLevel_Error:
            return "error";
        default:
            return "log";
    }
}

void LogSetConsoleOutput(b32 enabled)
{
    log_console_enabled = enabled;
}

static Arena *LogScratch(void)
{
    if (log_thread.scratch == 0)
    {
        log_thread.scratch = ArenaAlloc(LOG_SCRATCH_RESERVE);
    }
    return log_thread.scratch;
}

// text ends in '\n'. Captured into the innermost scope without it.
static void LogEmit(String8 text)
{
    LogScope *scope = log_thread.top_scope;
    if (scope != 0)
    {
        String8 copy = Str8Copy(scope->arena, Str8Chop(text, 1));
        Str8ListPush(scope->arena, &scope->messages, copy);
    }
    if (OsDebuggerPresent())
    {
        OsDebuggerWrite(text);
    }
    if (log_console_enabled)
    {
        OsStdErrWrite(text);
    }
}

static void LogWriteLineV(const char *file,
                          int line,
                          const char *label,
                          const char *fmt,
                          va_list args)
{
    if (log_thread.in_log)
    {
        return;
    }
    log_thread.in_log = 1;

    Arena *scratch = LogScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    String8 message = Str8PushFV(scratch, fmt, args);
    String8 text =
      Str8PushF(scratch, "%s(%d): %s: %S\n", file, line, label, message);
    if (text.size == 0)
    {
        text = STR8_LIT("log: out of memory, message dropped\n");
    }
    LogEmit(text);
    ArenaTempEnd(temp);

    log_thread.in_log = 0;
}

static void LogWriteLineF(const char *file,
                          int line,
                          const char *label,
                          const char *fmt,
                          ...)
{
    va_list args;
    va_start(args, fmt);
    LogWriteLineV(file, line, label, fmt, args);
    va_end(args);
}

void LogMessageV(LogLevel level,
                 const char *file,
                 int line,
                 const char *fmt,
                 va_list args)
{
    LogWriteLineV(file, line, LogLevelName(level), fmt, args);
}

void LogMessage(LogLevel level,
                const char *file,
                int line,
                const char *fmt,
                ...)
{
    va_list args;
    va_start(args, fmt);
    LogMessageV(level, file, line, fmt, args);
    va_end(args);
}

void LogScopeBegin(Arena *arena)
{
    LogScope *scope = ArenaPushStruct<LogScope>(arena);
    if (ArenaPushFailed(scope))
    {
        if (log_thread.top_scope != 0)
        {
            ++log_thread.top_scope->dropped_children;
        }
        else
        {
            ++log_thread.dropped_scopes;
        }
        return;
    }
    scope->arena = arena;
    scope->parent = log_thread.top_scope;
    log_thread.top_scope = scope;
}

String8List LogScopeEnd(void)
{
    String8List result = {};
    LogScope *scope = log_thread.top_scope;
    if (scope != 0)
    {
        if (scope->dropped_children > 0)
        {
            --scope->dropped_children;
            return result;
        }
        log_thread.top_scope = scope->parent;
        return scope->messages;
    }
    if (log_thread.dropped_scopes > 0)
    {
        --log_thread.dropped_scopes;
    }
    return result;
}

static b32 LogAssertHook(const char *file,
                         int line,
                         const char *expression,
                         const char *message)
{
    const char *separator = "";
    if (message != 0 && message[0] != 0)
    {
        separator = ": ";
    }
    else
    {
        message = "";
    }
    LogWriteLineF(
      file, line, "assert", "%s%s%s", expression, separator, message);
    return 1;
}

static void LogArenaFailureHook(Arena *arena, u64 size, const char *reason)
{
    if (ArenaIsNil(arena))
    {
        LOG_WARN("ArenaAlloc of %llu bytes failed: %s; using the nil arena",
                 size,
                 reason);
    }
    else
    {
        LOG_WARN("arena %p: push of %llu bytes failed: %s (%llu of %llu bytes "
                 "used); further failures on this arena are not reported",
                 (void *)arena,
                 size,
                 reason,
                 ArenaUsed(arena),
                 arena->reserved);
    }
}

static void LogOsMemFailureHook(const char *operation,
                                void *ptr,
                                u64 size,
                                u32 os_error)
{
    if constexpr (LogLevel_Warning >= LOG_MIN_LEVEL)
    {
        if (log_thread.in_log)
        {
            return;
        }
        Arena *scratch = LogScratch();
        ArenaTemp temp = ArenaTempBegin(scratch);
        String8 reason = STR8_LIT("no OS error code");
        if (os_error != 0)
        {
            reason = OsErrorMessage(scratch, os_error);
        }
        LOG_WARN("OsMem %s of %llu bytes at %p failed: %S",
                 operation,
                 size,
                 ptr,
                 reason);
        ArenaTempEnd(temp);
    }
    else
    {
        (void)operation;
        (void)ptr;
        (void)size;
        (void)os_error;
    }
}

static void LogOsFileFailureHook(const char *operation,
                                 String8 path,
                                 u32 os_error)
{
    if constexpr (LogLevel_Warning >= LOG_MIN_LEVEL)
    {
        if (log_thread.in_log)
        {
            return;
        }
        Arena *scratch = LogScratch();
        ArenaTemp temp = ArenaTempBegin(scratch);
        String8 reason = STR8_LIT("no OS error code");
        if (os_error != 0)
        {
            reason = OsErrorMessage(scratch, os_error);
        }
        if (path.size > 0)
        {
            LOG_WARN("OsFile %s \"%S\" failed: %S", operation, path, reason);
        }
        else
        {
            LOG_WARN("OsFile %s failed: %S", operation, reason);
        }
        ArenaTempEnd(temp);
    }
    else
    {
        (void)operation;
        (void)path;
        (void)os_error;
    }
}

static b32 LogInstallHooks(void)
{
    if (base_assert_hook == 0)
    {
        base_assert_hook = LogAssertHook;
    }
    if (arena_failure_hook == 0)
    {
        arena_failure_hook = LogArenaFailureHook;
    }
    if (os_mem_failure_hook == 0)
    {
        os_mem_failure_hook = LogOsMemFailureHook;
    }
    if (os_file_failure_hook == 0)
    {
        os_file_failure_hook = LogOsFileFailureHook;
    }
#if !defined(BASE_NO_CRASH_HANDLER)
    OsCrashHandlerInstall();
#endif
    return 1;
}

void FatalError(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list print_args;
    va_copy(print_args, args);
    LogWriteLineV(file, line, "fatal", fmt, print_args);
    va_end(print_args);

    // The scratch is never reset: the message must outlive the raise.
    Arena *scratch = LogScratch();
    String8 message = Str8PushFV(scratch, fmt, args);
    va_end(args);
    String8 report = Str8PushF(scratch, "%s(%d): %S", file, line, message);
    if (report.size == 0)
    {
        report = Str8FromCStr(fmt);
    }
    OsCrashRaise(OS_CRASH_FATAL_ERROR, report);
}

[[maybe_unused]] static b32 log_hooks_installed = LogInstallHooks();
