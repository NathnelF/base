/*
    os_console_win32.cpp

    Windows backend for os_console.h. Included by os_console.cpp.

    A stream is a console when GetConsoleMode succeeds on its handle.
    Console output goes through OsWin32WriteWide (os_debug_win32.cpp, which
    os.cpp includes first) and console input through ReadConsoleW; files and
    pipes use WriteFile / ReadFile on the raw UTF-8 bytes.

    Input lines are gathered as pieces in the per-thread console scratch and
    joined into the caller's arena once, so only the finished line lands
    there. A high surrogate at the end of a ReadConsoleW chunk is carried
    into the next chunk so a pair is never split.
*/

#if OS_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

constexpr u64 OS_WIN32_STD_WRITE_CHUNK = 1ull << 30;
constexpr u64 OS_WIN32_STDIN_CONSOLE_CHUNK = 1024;
constexpr u64 OS_WIN32_STDIN_BUFFER_SIZE = 4096;

struct OsWin32StdIn
{
    u8 buffer[OS_WIN32_STDIN_BUFFER_SIZE];
    u64 start;
    u64 end;
    b32 started;
    b32 eof;
};

static OsWin32StdIn os_win32_stdin;

static void OsWin32StdWrite(DWORD std_handle_id, String8 text)
{
    HANDLE handle = GetStdHandle(std_handle_id);
    if (handle == 0 || handle == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode))
    {
        OsWin32WriteWide(text, handle);
        return;
    }
    // Redirected to a file or pipe: write the UTF-8 bytes unchanged.
    u64 offset = 0;
    while (offset < text.size)
    {
        u64 chunk = text.size - offset;
        if (chunk > OS_WIN32_STD_WRITE_CHUNK)
        {
            chunk = OS_WIN32_STD_WRITE_CHUNK;
        }
        DWORD written = 0;
        if (!WriteFile(handle, text.data + offset, (DWORD)chunk, &written, 0) ||
            written == 0)
        {
            return;
        }
        offset += written;
    }
}

void OsStdOutWrite(String8 text)
{
    OsWin32StdWrite(STD_OUTPUT_HANDLE, text);
}

void OsStdErrWrite(String8 text)
{
    OsWin32StdWrite(STD_ERROR_HANDLE, text);
}

// Reads console input up to and including '\n' into pieces. Returns false
// at end of input (Ctrl+Z line, Ctrl+C, failure) with nothing gathered.
static b32 OsWin32StdInReadConsole(HANDLE handle,
                                   Arena *scratch,
                                   String8List *pieces)
{
    u16 buffer[OS_WIN32_STDIN_CONSOLE_CHUNK];
    u64 carry = 0;
    b32 first = 1;
    for (;;)
    {
        DWORD got = 0;
        DWORD room = (DWORD)(OS_WIN32_STDIN_CONSOLE_CHUNK - carry);
        if (!ReadConsoleW(handle, buffer + carry, room, &got, 0) || got == 0)
        {
            return pieces->count != 0;
        }
        u64 count = carry + got;
        if (first && buffer[0] == 0x1A)
        {
            return 0;
        }
        first = 0;
        carry = 0;
        u16 last = buffer[count - 1];
        if (last >= 0xD800 && last <= 0xDBFF)
        {
            carry = 1;
            --count;
        }
        String16 wide = {};
        wide.data = buffer;
        wide.size = count;
        Str8ListPush(scratch, pieces, Str8From16(scratch, wide));
        if (carry != 0)
        {
            buffer[0] = last;
        }
        else if (last == '\n')
        {
            return 1;
        }
    }
}

// Reads redirected input up to and including '\n' into pieces, through the
// process-wide buffer. Returns false at end of input with nothing gathered.
static b32 OsWin32StdInReadFile(HANDLE handle,
                                Arena *scratch,
                                String8List *pieces)
{
    OsWin32StdIn *in = &os_win32_stdin;
    for (;;)
    {
        String8 pending = Str8Make(in->buffer + in->start, in->end - in->start);
        u64 newline = Str8FindByte(pending, '\n', 0);
        if (newline < pending.size)
        {
            Str8ListPush(scratch,
                         pieces,
                         Str8Copy(scratch, Str8Prefix(pending, newline + 1)));
            in->start += newline + 1;
            return 1;
        }
        if (pending.size != 0)
        {
            Str8ListPush(scratch, pieces, Str8Copy(scratch, pending));
        }
        in->start = 0;
        in->end = 0;

        DWORD got = 0;
        if (!ReadFile(
              handle, in->buffer, OS_WIN32_STDIN_BUFFER_SIZE, &got, 0) ||
            got == 0)
        {
            return pieces->count != 0;
        }
        in->end = got;
        if (!in->started)
        {
            in->started = 1;
            if (got >= 3 && in->buffer[0] == 0xEF && in->buffer[1] == 0xBB &&
                in->buffer[2] == 0xBF)
            {
                in->start = 3;
            }
        }
    }
}

String8 OsStdInReadLine(Arena *arena, b32 *out_eof)
{
    String8 line = {};
    b32 eof = os_win32_stdin.eof;
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == 0 || handle == INVALID_HANDLE_VALUE)
    {
        eof = 1;
    }
    if (!eof)
    {
        Arena *scratch = OsConsoleScratch();
        ArenaTemp temp = ArenaTempBegin(scratch);
        String8List pieces = {};
        b32 got_line = 0;
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode))
        {
            got_line = OsWin32StdInReadConsole(handle, scratch, &pieces);
        }
        else
        {
            got_line = OsWin32StdInReadFile(handle, scratch, &pieces);
        }
        if (got_line)
        {
            String8 joined = Str8Join(scratch, &pieces, STR8_LIT(""));
            if (Str8EndsWith(joined, STR8_LIT("\n")))
            {
                joined = Str8Chop(joined, 1);
            }
            if (Str8EndsWith(joined, STR8_LIT("\r")))
            {
                joined = Str8Chop(joined, 1);
            }
            line = Str8Copy(arena, joined);
        }
        else
        {
            eof = 1;
            os_win32_stdin.eof = 1;
        }
        ArenaTempEnd(temp);
    }
    if (out_eof != 0)
    {
        *out_eof = eof;
    }
    return line;
}

#endif
