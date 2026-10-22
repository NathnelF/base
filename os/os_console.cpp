/*
    os_console.cpp

    Platform-independent half of os_console.h: Print / PrintV and the
    per-thread scratch they format into (which the backend also uses to
    gather input lines). The backend included at the end provides the
    stream reads and writes.
*/

#include "os_console.h"

constexpr u64 OS_CONSOLE_SCRATCH_RESERVE = 1ull << 30;

static thread_local Arena *os_console_scratch;

static Arena *OsConsoleScratch(void)
{
    if (os_console_scratch == 0)
    {
        os_console_scratch = ArenaAlloc(OS_CONSOLE_SCRATCH_RESERVE);
    }
    return os_console_scratch;
}

void PrintV(const char *fmt, va_list args)
{
    Arena *scratch = OsConsoleScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    OsStdOutWrite(Str8PushFV(scratch, fmt, args));
    ArenaTempEnd(temp);
}

void Print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    PrintV(fmt, args);
    va_end(args);
}

#include "os_console_win32.cpp"

#if !OS_WINDOWS
#error "os_console: no backend for this OS yet"
#endif
