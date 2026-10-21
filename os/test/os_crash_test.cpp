/*
    os_crash_test.cpp

    Exercises crash handling. The test re-runs its own executable as a child
    process with "--crash <mode>"; the child crashes in that way, and the
    parent checks the child's exit code, the report the crash handler wrote
    to stderr (header, a frame in this file naming the crashing function,
    the minidump line), and that the minidump exists. Dumps are deleted
    afterwards. It also checks OsStackTraceHere without crashing.

    Prints FAIL lines and returns nonzero if anything failed. Build with
    `fbuild os-crash-test-debug` (or -release / -profile) from the repo
    root. Includes base.cpp, so the log layer installs the crash handler.

    NOTE: Crash functions are noinline so their frames survive /O2 and the
    check for the function name holds in every config.

    NOTE: Uses printf until the os layer has its own output functions.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../base.cpp"

static int test_failures = 0;

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                \
            ++test_failures;                                                   \
        }                                                                      \
    } while (0)

// The crashes, run in the child process.

static __declspec(noinline) void CrashNullWrite(void)
{
    volatile int *pointer = 0;
    *pointer = 1;
}

static __declspec(noinline) void CrashFatal(void)
{
    FATAL_ERROR("fatal test %d", 7);
}

static __declspec(noinline) void CrashAssert(void)
{
    volatile int zero = 0;
    ASSERT_ALWAYS_MSG(zero == 1, "assert test");
}

#pragma warning(push)
#pragma warning(disable : 4717) // recursive on all control paths: on purpose
static __declspec(noinline) int CrashRecurse(int depth)
{
    volatile u8 buffer[512];
    buffer[0] = (u8)depth;
    return CrashRecurse(depth + 1) + buffer[0];
}
#pragma warning(pop)

static __declspec(noinline) void CrashAbort(void)
{
    abort();
}

static __declspec(noinline) void CrashInvalidParameter(void)
{
    // Keep the result so the call is not a tail call: /O2 would otherwise
    // jump to _itoa_s and drop this frame from the trace.
    volatile errno_t result = _itoa_s(1, (char *)0, 0, 10);
    (void)result;
}

static int RunCrashMode(const char *mode)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (strcmp(mode, "null") == 0)
    {
        CrashNullWrite();
    }
    else if (strcmp(mode, "fatal") == 0)
    {
        CrashFatal();
    }
    else if (strcmp(mode, "assert") == 0)
    {
        CrashAssert();
    }
    else if (strcmp(mode, "overflow") == 0)
    {
        CrashRecurse(0);
    }
    else if (strcmp(mode, "abort") == 0)
    {
        CrashAbort();
    }
    else if (strcmp(mode, "invalid-parameter") == 0)
    {
        CrashInvalidParameter();
    }
    return 100; // no crash happened
}

// The parent side.

struct ChildResult
{
    u32 exit_code;
    String8 output;
};

static ChildResult RunChild(Arena *arena, const char *mode)
{
    ChildResult result = {};
    wchar_t exe[MAX_PATH * 2];
    DWORD exe_length = GetModuleFileNameW(0, exe, MAX_PATH * 2);
    String16 exe16 = { (u16 *)exe, exe_length };
    String8 command =
      Str8PushF(arena, "\"%S\" --crash %s", Str8From16(arena, exe16), mode);
    String16 command16 = Str16From8(arena, command);

    SECURITY_ATTRIBUTES inherit = {};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE read_pipe = 0;
    HANDLE write_pipe = 0;
    if (!CreatePipe(&read_pipe, &write_pipe, &inherit, 0))
    {
        return result;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    PROCESS_INFORMATION process = {};
    BOOL started = CreateProcessW(
      0, (wchar_t *)command16.data, 0, 0, TRUE, 0, 0, 0, &startup, &process);
    CloseHandle(write_pipe);
    if (!started)
    {
        CloseHandle(read_pipe);
        return result;
    }

    // Read until the child exits and the pipe closes, so a long report
    // never fills the pipe and blocks the child.
    String8List chunks = {};
    for (;;)
    {
        u8 buffer[4096];
        DWORD got = 0;
        if (!ReadFile(read_pipe, buffer, sizeof(buffer), &got, 0) || got == 0)
        {
            break;
        }
        Str8ListPush(arena, &chunks, Str8Copy(arena, Str8Make(buffer, got)));
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(process.hProcess, 60 * 1000);
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);

    result.exit_code = exit_code;
    result.output = Str8Join(arena, &chunks, STR8_LIT(""));
    return result;
}

static b32 TestHas(String8 haystack, const char *needle)
{
    return Str8Find(haystack, Str8FromCStr(needle), 0) < haystack.size;
}

// Checks the "*** minidump: <path>" line, that the file exists and is not
// empty, then deletes it.
static b32 TestMinidump(Arena *arena, String8 output)
{
    String8 marker = STR8_LIT("*** minidump: ");
    u64 at = Str8Find(output, marker, 0);
    if (at == output.size)
    {
        return 0;
    }
    String8 rest = Str8Skip(output, at + marker.size);
    String8 path = Str8Trim(Str8Prefix(rest, Str8FindByte(rest, '\n', 0)));
    String16 path16 = Str16From8(arena, path);
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    b32 exists = GetFileAttributesExW(
      (wchar_t *)path16.data, GetFileExInfoStandard, &data);
    b32 non_empty =
      exists && (data.nFileSizeLow != 0 || data.nFileSizeHigh != 0);
    if (exists)
    {
        DeleteFileW((wchar_t *)path16.data);
    }
    return non_empty;
}

static void TestCrash(Arena *arena,
                      const char *mode,
                      u32 expected_code,
                      const char **expected)
{
    ChildResult child = RunChild(arena, mode);
    b32 ok = child.exit_code == expected_code;
    if (!ok)
    {
        printf("FAIL %s: exit code 0x%08X, expected 0x%08X\n",
               mode,
               child.exit_code,
               expected_code);
    }
    for (const char **text = expected; *text != 0; ++text)
    {
        if (!TestHas(child.output, *text))
        {
            printf("FAIL %s: report is missing \"%s\"\n", mode, *text);
            ok = 0;
        }
    }
    if (!TestMinidump(arena, child.output))
    {
        printf("FAIL %s: no minidump\n", mode);
        ok = 0;
    }
    if (!ok)
    {
        ++test_failures;
        printf("---- %s output ----\n%.*s----\n",
               mode,
               (int)child.output.size,
               (const char *)child.output.data);
    }
}

static __declspec(noinline) String8 TraceHelper(Arena *arena)
{
    return OsStackTraceHere(arena, 0);
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--crash") == 0)
    {
        return RunCrashMode(argv[2]);
    }

    Arena *arena = ArenaAllocDefault();

    // Stack trace without crashing: first frame is the caller.
    String8 trace = TraceHelper(arena);
    String8 first = Str8Prefix(trace, Str8FindByte(trace, '\n', 0));
    CHECK(TestHas(first, "os_crash_test.cpp("));
    CHECK(TestHas(first, "TraceHelper"));
    CHECK(TestHas(trace, "main"));

    const char *null_expected[] = { "*** crash: EXCEPTION_ACCESS_VIOLATION "
                                    "(0xC0000005): write to address 0x0",
                                    "os_crash_test.cpp(",
                                    "CrashNullWrite",
                                    0 };
    TestCrash(arena, "null", 0xC0000005, null_expected);

    const char *fatal_expected[] = {
        "): fatal: fatal test 7", "*** fatal error: ", "CrashFatal", 0
    };
    TestCrash(arena, "fatal", OS_CRASH_FATAL_ERROR, fatal_expected);

    const char *assert_expected[] = { "): assert: zero == 1: assert test",
                                      "EXCEPTION_BREAKPOINT",
                                      "CrashAssert",
                                      0 };
    TestCrash(arena, "assert", 0x80000003, assert_expected);

    const char *overflow_expected[] = { "EXCEPTION_STACK_OVERFLOW",
                                        "CrashRecurse",
                                        0 };
    TestCrash(arena, "overflow", 0xC00000FD, overflow_expected);

    const char *abort_expected[] = { "*** C runtime error: abort() was called",
                                     "CrashAbort",
                                     0 };
    TestCrash(arena, "abort", OS_CRASH_CRT_ERROR, abort_expected);

    const char *invalid_expected[] = { "*** C runtime error: invalid parameter",
                                       "CrashInvalidParameter",
                                       0 };
    TestCrash(arena, "invalid-parameter", OS_CRASH_CRT_ERROR, invalid_expected);

    ArenaRelease(arena);

    if (test_failures == 0)
    {
        printf("os crash: all tests passed\n");
    }
    return test_failures;
}
