/*
    os_console_test.cpp

    Exercises os_console.h with redirected streams. The test re-runs its own
    executable as a child process with "--print" or "--echo", with stdin fed
    from a pipe or a file and stdout / stderr captured through pipes:
    - print: Print output (integers, String8, C strings, UTF-8 text) arrives
      on stdout byte for byte and nothing goes to stderr.
    - echo: the child reads stdin a line at a time and prints each as
      [line]. Covers "\r\n" and "\n" endings, a last line with no newline,
      an empty line, a leading BOM, a line longer than the input buffer, and
      end of input staying reported.

    The console paths (WriteConsoleW / ReadConsoleW) can't be driven from
    here; check them by hand by running `os_console_test.exe --echo` in a
    terminal (Ctrl+Z, Enter ends input).

    Prints FAIL lines and returns nonzero if anything failed. Build with
    `fbuild os-console-test-debug` (or -release / -profile) from the repo
    root.
*/

#include <string.h>

#include "../../base.cpp"

static int test_failures = 0;

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            Print("FAIL line %d: %s\n", __LINE__, #condition);                 \
            ++test_failures;                                                   \
        }                                                                      \
    } while (0)

// The child side.

static int ChildPrint(void)
{
    String8 name = STR8_LIT("base");
    Print("int %d, unsigned %u, hex %x\n", -42, 7u, 255u);
    Print("str8 %S, cstr %s\n", name, "plain");
    Print("utf8 h\xC3\xA9llo \xE2\x9C\x93\n");
    return 0;
}

static int ChildEcho(void)
{
    Arena *arena = ArenaAllocDefault();
    b32 eof = 0;
    for (String8 line = OsStdInReadLine(arena, &eof); !eof;
         line = OsStdInReadLine(arena, &eof))
    {
        Print("[%S]", line);
    }
    // End of input stays reported, and out_eof may be null.
    String8 again = OsStdInReadLine(arena, 0);
    OsStdInReadLine(arena, &eof);
    if (again.size == 0 && eof)
    {
        Print("<eof>");
    }
    return 0;
}

// The parent side.

struct ChildResult
{
    String8 out;
    String8 err;
};

static String8 ReadAll(Arena *arena, HANDLE pipe)
{
    String8List chunks = {};
    for (;;)
    {
        u8 buffer[4096];
        DWORD got = 0;
        if (!ReadFile(pipe, buffer, sizeof(buffer), &got, 0) || got == 0)
        {
            break;
        }
        Str8ListPush(arena, &chunks, Str8Copy(arena, Str8Make(buffer, got)));
    }
    return Str8Join(arena, &chunks, STR8_LIT(""));
}

// Runs the child with input on stdin, from a pipe or (from_file) a file.
static ChildResult RunChild(Arena *arena,
                            const char *mode,
                            String8 input,
                            b32 from_file)
{
    ChildResult result = {};
    wchar_t exe[MAX_PATH * 2];
    DWORD exe_length = GetModuleFileNameW(0, exe, MAX_PATH * 2);
    String16 exe16 = { (u16 *)exe, exe_length };
    String8 exe8 = Str8From16(arena, exe16);
    String8 command = Str8PushF(arena, "\"%S\" %s", exe8, mode);
    String16 command16 = Str16From8(arena, command);

    SECURITY_ATTRIBUTES inherit = {};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE out_read = 0;
    HANDLE out_write = 0;
    HANDLE err_read = 0;
    HANDLE err_write = 0;
    HANDLE in_read = 0;
    HANDLE in_write = 0;
    CreatePipe(&out_read, &out_write, &inherit, 0);
    CreatePipe(&err_read, &err_write, &inherit, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

    String8 input_path = Str8PushF(arena, "%S.stdin.txt", exe8);
    if (from_file)
    {
        OsFileWrite(input_path, input, 0);
        String16 path16 = Str16From8(arena, input_path);
        in_read = CreateFileW((wchar_t *)path16.data,
                              GENERIC_READ,
                              FILE_SHARE_READ,
                              &inherit,
                              OPEN_EXISTING,
                              0,
                              0);
    }
    else
    {
        CreatePipe(&in_read, &in_write, &inherit, 0);
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = in_read;
    startup.hStdOutput = out_write;
    startup.hStdError = err_write;
    PROCESS_INFORMATION process = {};
    BOOL started = CreateProcessW(
      0, (wchar_t *)command16.data, 0, 0, TRUE, 0, 0, 0, &startup, &process);
    CloseHandle(in_read);
    CloseHandle(out_write);
    CloseHandle(err_write);

    if (started)
    {
        // Inputs here fit in the pipe buffer, so writing all of it before
        // reading cannot deadlock.
        if (in_write != 0)
        {
            DWORD written = 0;
            WriteFile(in_write, input.data, (DWORD)input.size, &written, 0);
            CloseHandle(in_write);
        }
        // The child writes little to stderr, so draining stdout first is safe.
        result.out = ReadAll(arena, out_read);
        result.err = ReadAll(arena, err_read);
        WaitForSingleObject(process.hProcess, 60 * 1000);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    }
    else if (in_write != 0)
    {
        CloseHandle(in_write);
    }
    CloseHandle(out_read);
    CloseHandle(err_read);
    if (from_file)
    {
        OsFileDelete(input_path, 0);
    }
    return result;
}

static void TestPrint(Arena *arena)
{
    ChildResult child = RunChild(arena, "--print", STR8_LIT(""), 0);
    String8 expected = STR8_LIT("int -42, unsigned 7, hex ff\n"
                                "str8 base, cstr plain\n"
                                "utf8 h\xC3\xA9llo \xE2\x9C\x93\n");
    CHECK(Str8Equal(child.out, expected));
    CHECK(child.err.size == 0);
}

static void TestEcho(Arena *arena, b32 from_file)
{
    String8 input = STR8_LIT("\xEF\xBB\xBFone\r\ntwo\n\nthree");
    ChildResult child = RunChild(arena, "--echo", input, from_file);
    CHECK(Str8Equal(child.out, STR8_LIT("[one][two][][three]<eof>")));

    // Empty input: end right away.
    child = RunChild(arena, "--echo", STR8_LIT(""), from_file);
    CHECK(Str8Equal(child.out, STR8_LIT("<eof>")));

    // A BOM only matters at the very start.
    child = RunChild(arena,
                     "--echo",
                     STR8_LIT("a\n\xEF\xBB\xBF"
                              "b\n"),
                     from_file);
    CHECK(Str8Equal(child.out,
                    STR8_LIT("[a][\xEF\xBB\xBF"
                             "b]<eof>")));
}

// A line longer than the 4K input buffer, from a file (too big for a pipe
// written before reading).
static void TestLongLine(Arena *arena)
{
    u64 size = 10000;
    u8 *data = ArenaPushArray<u8>(arena, size + 6);
    memset(data, 'x', size);
    memcpy(data + size, "\r\nend\n", 6);
    ChildResult child = RunChild(arena, "--echo", Str8Make(data, size + 6), 1);
    CHECK(child.out.size == size + 2 + 5 + 5);
    CHECK(Str8StartsWith(child.out, STR8_LIT("[xxxx")));
    CHECK(Str8EndsWith(child.out, STR8_LIT("x][end]<eof>")));
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--print") == 0)
    {
        return ChildPrint();
    }
    if (argc >= 2 && strcmp(argv[1], "--echo") == 0)
    {
        return ChildEcho();
    }

    Arena *arena = ArenaAllocDefault();
    TestPrint(arena);
    TestEcho(arena, 0);
    TestEcho(arena, 1);
    TestLongLine(arena);
    ArenaRelease(arena);

    if (test_failures == 0)
    {
        Print("os console: all tests passed\n");
    }
    return test_failures;
}
