/*
    os_crash_win32.cpp

    Windows backend for os_crash.h. Included by os_crash.cpp.

    Flow on a crash:
    1. OsWin32CrashFilter runs on the crashing thread. It does as little as
       possible: records the exception, starts a report thread with its own
       1MB stack, waits for it, then terminates the process with the
       exception code. Doing the work on another thread is what lets a
       stack overflow be reported at all.
    2. OsWin32CrashReport (on the report thread) writes the header, walks the
       crashed thread's stack from the exception's CONTEXT with StackWalk64,
       writes the minidump, and calls os_crash_hook. Each line is written as
       soon as it is formatted, so a hang or second crash in symbol loading
       still leaves the header behind.

    A second thread crashing while a report is in progress waits forever;
    the first report ends the process. A crash on the report thread itself
    terminates immediately, and the filter stops waiting after
    OS_WIN32_CRASH_REPORT_TIMEOUT_MS either way.

    Formatting uses an arena reserved at install time (ArenaAlloc maps fresh
    pages with VirtualAlloc, so a corrupted heap does not matter). If that
    arena is nil the lines degrade to their fixed parts.

    NOTE: dbghelp is not thread-safe. OsStackTraceHere serializes on a lock;
    the crash path does not take it (it must not block), which is acceptable
    for a process that is about to exit.

    NOTE: In debug CRT builds, CRT assertion and error reports are routed to
    stderr and the debugger instead of a modal dialog, so invalid-parameter
    and abort() failures reach the crash handler unattended.
*/

#if OS_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <crtdbg.h>
#include <dbghelp.h>
#include <signal.h>
#include <stdlib.h>

constexpr u64 OS_WIN32_CRASH_ARENA_RESERVE = 64ull * 1024 * 1024;
constexpr u32 OS_WIN32_CRASH_REPORT_STACK = 1024 * 1024;
constexpr u32 OS_WIN32_CRASH_REPORT_TIMEOUT_MS = 60 * 1000;
constexpr ULONG OS_WIN32_CRASH_STACK_GUARANTEE = 64 * 1024;
constexpr u32 OS_WIN32_MAX_FRAMES = 64;

struct OsWin32DbgHelp
{
    HMODULE module;
    b32 initialized;
    b32 failed;
    SRWLOCK lock;
    decltype(&SymSetOptions) sym_set_options;
    decltype(&SymInitialize) sym_initialize;
    decltype(&SymFromAddr) sym_from_addr;
    decltype(&SymGetLineFromAddr64) sym_get_line_from_addr64;
    decltype(&SymFunctionTableAccess64) sym_function_table_access64;
    decltype(&SymGetModuleBase64) sym_get_module_base64;
    decltype(&StackWalk64) stack_walk64;
    decltype(&MiniDumpWriteDump) mini_dump_write_dump;
};

struct OsWin32CrashState
{
    b32 installed;
    Arena *arena;
    EXCEPTION_POINTERS *exception;
    DWORD thread_id;
    volatile DWORD report_thread_id;
    volatile LONG in_progress;
};

static OsWin32DbgHelp os_win32_dbghelp;
static OsWin32CrashState os_win32_crash;

template<typename T>
static b32 OsWin32LoadProc(HMODULE module, const char *name, T *out)
{
    *out = (T)(void *)GetProcAddress(module, name);
    return *out != 0;
}

// Loads dbghelp from System32 and initializes symbols once. Returns false
// (and stays false) if that is not possible.
static b32 OsWin32DbgHelpLoad(void)
{
    OsWin32DbgHelp *d = &os_win32_dbghelp;
    if (d->initialized)
    {
        return 1;
    }
    if (d->failed)
    {
        return 0;
    }
    d->failed = 1;
    d->module = LoadLibraryExW(L"dbghelp.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (d->module == 0)
    {
        return 0;
    }
    HMODULE m = d->module;
    b32 ok =
      OsWin32LoadProc(m, "SymSetOptions", &d->sym_set_options) &&
      OsWin32LoadProc(m, "SymInitialize", &d->sym_initialize) &&
      OsWin32LoadProc(m, "SymFromAddr", &d->sym_from_addr) &&
      OsWin32LoadProc(
        m, "SymGetLineFromAddr64", &d->sym_get_line_from_addr64) &&
      OsWin32LoadProc(
        m, "SymFunctionTableAccess64", &d->sym_function_table_access64) &&
      OsWin32LoadProc(m, "SymGetModuleBase64", &d->sym_get_module_base64) &&
      OsWin32LoadProc(m, "StackWalk64", &d->stack_walk64) &&
      OsWin32LoadProc(m, "MiniDumpWriteDump", &d->mini_dump_write_dump);
    if (!ok)
    {
        return 0;
    }
    d->sym_set_options(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS |
                       SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!d->sym_initialize(GetCurrentProcess(), 0, TRUE))
    {
        return 0;
    }
    d->failed = 0;
    d->initialized = 1;
    return 1;
}

static String8 OsWin32ModuleFileName(Arena *arena, HMODULE module)
{
    wchar_t buffer[MAX_PATH * 2];
    DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH * 2);
    String16 wide = {};
    wide.data = (u16 *)buffer;
    wide.size = length;
    return Str8From16(arena, wide);
}

// Appends line + '\n' to out; with echo, also writes it out immediately.
static void OsWin32EmitLine(Arena *arena,
                            String8List *out,
                            b32 echo,
                            String8 line)
{
    String8 text = Str8Cat(arena, line, STR8_LIT("\n"));
    if (echo)
    {
        if (text.size > 0)
        {
            OsStdErrWrite(text);
            OsDebuggerWrite(text);
        }
        else
        {
            OsStdErrWrite(line);
            OsStdErrWrite(STR8_LIT("\n"));
        }
    }
    Str8ListPush(arena, out, text);
}

// "file(line): function", or "0xADDRESS: module+0xOFFSET function" when
// there is no line information.
static String8 OsWin32FrameLine(Arena *arena, DWORD64 address)
{
    OsWin32DbgHelp *d = &os_win32_dbghelp;
    HANDLE process = GetCurrentProcess();

    u64 symbol_storage[(sizeof(SYMBOL_INFO) + 256 + 7) / 8] = {};
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)symbol_storage;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 symbol_displacement = 0;
    const char *name = "?";
    if (d->sym_from_addr(process, address, &symbol_displacement, symbol))
    {
        name = symbol->Name;
    }

    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(line);
    DWORD line_displacement = 0;
    if (d->sym_get_line_from_addr64(
          process, address, &line_displacement, &line))
    {
        return Str8PushF(
          arena, "%s(%u): %s", line.FileName, (u32)line.LineNumber, name);
    }

    HMODULE module = 0;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)address,
                       &module);
    String8 module_name = STR8_LIT("?");
    if (module != 0)
    {
        String8 path = OsWin32ModuleFileName(arena, module);
        u64 slash = Str8FindLast(path, STR8_LIT("\\"));
        if (slash < path.size)
        {
            path = Str8Skip(path, slash + 1);
        }
        module_name = path;
    }
    return Str8PushF(arena,
                     "0x%016llX: %S+0x%llX %s",
                     (u64)address,
                     module_name,
                     (u64)(address - (DWORD64)module),
                     name);
}

// Walks a stack from context (which it modifies), one line per frame.
static void OsWin32WalkStack(Arena *arena,
                             String8List *out,
                             b32 echo,
                             HANDLE thread,
                             CONTEXT *context,
                             u32 skip_frames)
{
    OsWin32DbgHelp *d = &os_win32_dbghelp;
    if (!OsWin32DbgHelpLoad())
    {
        OsWin32EmitLine(arena,
                        out,
                        echo,
                        STR8_LIT("(no stack trace: dbghelp.dll unavailable)"));
        return;
    }

    STACKFRAME64 frame = {};
    DWORD machine = 0;
#if ARCH_X64
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context->Rip;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrFrame.Offset = context->Rbp;
#elif ARCH_ARM64
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context->Pc;
    frame.AddrStack.Offset = context->Sp;
    frame.AddrFrame.Offset = context->Fp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;

    HANDLE process = GetCurrentProcess();
    for (u32 index = 0; index < OS_WIN32_MAX_FRAMES + skip_frames; ++index)
    {
        if (!d->stack_walk64(machine,
                             process,
                             thread,
                             &frame,
                             context,
                             0,
                             d->sym_function_table_access64,
                             d->sym_get_module_base64,
                             0))
        {
            break;
        }
        DWORD64 address = frame.AddrPC.Offset;
        if (address == 0)
        {
            break;
        }
        if (index < skip_frames)
        {
            continue;
        }
        // Past the first frame the PC is a return address, one past the
        // call; step back into the call so the line number is the call's.
        DWORD64 lookup = address;
        if (index > 0)
        {
            lookup -= 1;
        }
        OsWin32EmitLine(arena, out, echo, OsWin32FrameLine(arena, lookup));
    }
}

static const char *OsWin32ExceptionName(u32 code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:
            return "EXCEPTION_BREAKPOINT (a failed ASSERT_ALWAYS or "
                   "__debugbreak without a debugger)";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION:
            return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:
            return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:
            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:
            return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:
            return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_GUARD_PAGE:
            return "EXCEPTION_GUARD_PAGE";
        default:
            return "unknown exception";
    }
}

static String8 OsWin32CrashHeader(Arena *arena, EXCEPTION_RECORD *record)
{
    u32 code = record->ExceptionCode;
    if ((code == OS_CRASH_FATAL_ERROR || code == OS_CRASH_CRT_ERROR) &&
        record->NumberParameters >= 2)
    {
        String8 message = Str8Make((u8 *)record->ExceptionInformation[0],
                                   (u64)record->ExceptionInformation[1]);
        const char *label = "fatal error";
        if (code == OS_CRASH_CRT_ERROR)
        {
            label = "C runtime error";
        }
        return Str8PushF(arena, "*** %s: %S", label, message);
    }

    String8 detail = {};
    if ((code == EXCEPTION_ACCESS_VIOLATION ||
         code == EXCEPTION_IN_PAGE_ERROR) &&
        record->NumberParameters >= 2)
    {
        const char *access = "read of";
        if (record->ExceptionInformation[0] == 1)
        {
            access = "write to";
        }
        else if (record->ExceptionInformation[0] == 8)
        {
            access = "execution of";
        }
        detail = Str8PushF(arena,
                           ": %s address 0x%llX",
                           access,
                           (u64)record->ExceptionInformation[1]);
    }
    return Str8PushF(arena,
                     "*** crash: %s (0x%08X)%S",
                     OsWin32ExceptionName(code),
                     code,
                     detail);
}

// <exe without .exe>.crash-YYYYMMDD-HHMMSS-<pid>.dmp next to the executable
// (the pid keeps crashes within one second apart).
// Returns the path, or empty if the dump could not be written.
static String8 OsWin32WriteMinidump(Arena *arena, String8List *report)
{
    OsWin32DbgHelp *d = &os_win32_dbghelp;
    OsWin32CrashState *state = &os_win32_crash;
    String8 empty = {};
    if (!OsWin32DbgHelpLoad())
    {
        OsWin32EmitLine(arena,
                        report,
                        1,
                        STR8_LIT("*** minidump: not written, dbghelp.dll "
                                 "unavailable"));
        return empty;
    }

    String8 exe = OsWin32ModuleFileName(arena, 0);
    if (Str8EndsWith(Str8ToLower(arena, exe), STR8_LIT(".exe")))
    {
        exe = Str8Chop(exe, 4);
    }
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    String8 path = Str8PushF(arena,
                             "%S.crash-%04u%02u%02u-%02u%02u%02u-%u.dmp",
                             exe,
                             (u32)time.wYear,
                             (u32)time.wMonth,
                             (u32)time.wDay,
                             (u32)time.wHour,
                             (u32)time.wMinute,
                             (u32)time.wSecond,
                             (u32)GetCurrentProcessId());
    String16 path16 = Str16From8(arena, path);
    HANDLE file = INVALID_HANDLE_VALUE;
    if (path16.size > 0)
    {
        file = CreateFileW((wchar_t *)path16.data,
                           GENERIC_WRITE,
                           0,
                           0,
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           0);
    }
    if (file == INVALID_HANDLE_VALUE)
    {
        OsWin32EmitLine(arena,
                        report,
                        1,
                        Str8PushF(arena,
                                  "*** minidump: could not create %S: %S",
                                  path,
                                  OsLastErrorMessage(arena)));
        return empty;
    }

    MINIDUMP_EXCEPTION_INFORMATION info = {};
    info.ThreadId = state->thread_id;
    info.ExceptionPointers = state->exception;
    info.ClientPointers = FALSE;
    MINIDUMP_TYPE type =
      (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory |
                      MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    BOOL written = d->mini_dump_write_dump(
      GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, 0, 0);
    CloseHandle(file);
    if (!written)
    {
        DeleteFileW((wchar_t *)path16.data);
        OsWin32EmitLine(arena,
                        report,
                        1,
                        Str8PushF(arena,
                                  "*** minidump: MiniDumpWriteDump failed: %S",
                                  OsLastErrorMessage(arena)));
        return empty;
    }
    OsWin32EmitLine(
      arena, report, 1, Str8PushF(arena, "*** minidump: %S", path));
    return path;
}

static void OsWin32CrashReport(void)
{
    OsWin32CrashState *state = &os_win32_crash;
    Arena *arena = state->arena;
    String8List report = {};

    OsWin32EmitLine(
      arena,
      &report,
      1,
      OsWin32CrashHeader(arena, state->exception->ExceptionRecord));
    OsWin32EmitLine(arena,
                    &report,
                    1,
                    Str8PushF(arena,
                              "*** thread %u in %S",
                              (u32)state->thread_id,
                              OsWin32ModuleFileName(arena, 0)));

    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION |
                                 THREAD_SUSPEND_RESUME,
                               FALSE,
                               state->thread_id);
    CONTEXT context = *state->exception->ContextRecord;
    OsWin32WalkStack(arena, &report, 1, thread, &context, 0);
    if (thread != 0)
    {
        CloseHandle(thread);
    }

    String8 dump_path = OsWin32WriteMinidump(arena, &report);
    if (os_crash_hook != 0)
    {
        os_crash_hook(Str8Join(arena, &report, STR8_LIT("")), dump_path);
    }
}

static DWORD WINAPI OsWin32CrashReportThread(LPVOID parameter)
{
    (void)parameter;
    OsWin32CrashReport();
    return 0;
}

static LONG WINAPI OsWin32CrashFilter(EXCEPTION_POINTERS *exception)
{
    OsWin32CrashState *state = &os_win32_crash;
    DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (GetCurrentThreadId() == state->report_thread_id)
    {
        // Crashed while reporting: give up on the report.
        TerminateProcess(GetCurrentProcess(), code);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    if (InterlockedCompareExchange(&state->in_progress, 1, 0) != 0)
    {
        // Another thread is reporting; it ends the process.
        Sleep(INFINITE);
    }

    state->exception = exception;
    state->thread_id = GetCurrentThreadId();
    DWORD report_thread_id = 0;
    HANDLE thread = CreateThread(0,
                                 OS_WIN32_CRASH_REPORT_STACK,
                                 OsWin32CrashReportThread,
                                 0,
                                 CREATE_SUSPENDED,
                                 &report_thread_id);
    if (thread != 0)
    {
        state->report_thread_id = report_thread_id;
        ResumeThread(thread);
        WaitForSingleObject(thread, OS_WIN32_CRASH_REPORT_TIMEOUT_MS);
        CloseHandle(thread);
    }
    else
    {
        OsWin32CrashReport();
    }
    TerminateProcess(GetCurrentProcess(), code);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void __cdecl OsWin32InvalidParameterHandler(const wchar_t *expression,
                                                   const wchar_t *function,
                                                   const wchar_t *file,
                                                   unsigned int line,
                                                   uintptr_t reserved)
{
    (void)reserved;
    Arena *arena = os_win32_crash.arena;
    String8 message = STR8_LIT("invalid parameter passed to a C runtime "
                               "function");
    // Debug CRTs pass the failed check; release CRTs pass nulls.
    if (expression != 0 && function != 0 && file != 0)
    {
        String16 expression16 = { (u16 *)expression, 0 };
        String16 function16 = { (u16 *)function, 0 };
        String16 file16 = { (u16 *)file, 0 };
        while (expression[expression16.size] != 0)
        {
            ++expression16.size;
        }
        while (function[function16.size] != 0)
        {
            ++function16.size;
        }
        while (file[file16.size] != 0)
        {
            ++file16.size;
        }
        String8 detailed = Str8PushF(arena,
                                     "invalid parameter: %S in %S (%S(%u))",
                                     Str8From16(arena, expression16),
                                     Str8From16(arena, function16),
                                     Str8From16(arena, file16),
                                     line);
        if (detailed.size > 0)
        {
            message = detailed;
        }
    }
    OsCrashRaise(OS_CRASH_CRT_ERROR, message);
}

static void __cdecl OsWin32PureCallHandler(void)
{
    OsCrashRaise(OS_CRASH_CRT_ERROR, STR8_LIT("pure virtual function call"));
}

static void __cdecl OsWin32AbortHandler(int signal_number)
{
    (void)signal_number;
    OsCrashRaise(OS_CRASH_CRT_ERROR, STR8_LIT("abort() was called"));
}

void OsCrashHandlerInstall(void)
{
    OsWin32CrashState *state = &os_win32_crash;
    if (state->installed)
    {
        return;
    }
    state->installed = 1;
    state->arena = ArenaAlloc(OS_WIN32_CRASH_ARENA_RESERVE);

    SetUnhandledExceptionFilter(OsWin32CrashFilter);
    ULONG guarantee = OS_WIN32_CRASH_STACK_GUARANTEE;
    SetThreadStackGuarantee(&guarantee);

    _set_invalid_parameter_handler(OsWin32InvalidParameterHandler);
    _set_purecall_handler(OsWin32PureCallHandler);
    signal(SIGABRT, OsWin32AbortHandler);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    // Debug CRT reports would otherwise open a modal dialog first.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
}

void OsCrashRaise(u32 code, String8 message)
{
    ULONG_PTR arguments[2] = { (ULONG_PTR)message.data,
                               (ULONG_PTR)message.size };
    RaiseException(code, EXCEPTION_NONCONTINUABLE, 2, arguments);
    // Only reached if something handled a noncontinuable exception.
    ExitProcess(code);
}

__declspec(noinline) String8 OsStackTraceHere(Arena *arena, u32 skip_frames)
{
    OsWin32DbgHelp *d = &os_win32_dbghelp;
    CONTEXT context = {};
    RtlCaptureContext(&context);
    String8List lines = {};
    AcquireSRWLockExclusive(&d->lock);
    // Frame 0 is this function.
    OsWin32WalkStack(
      arena, &lines, 0, GetCurrentThread(), &context, skip_frames + 1);
    ReleaseSRWLockExclusive(&d->lock);
    return Str8Join(arena, &lines, STR8_LIT(""));
}

#endif
