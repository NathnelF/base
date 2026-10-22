/*
    os_debug_win32.cpp

    Windows backend for os_debug.h. Included by os_debug.cpp.

    UTF-8 output is converted to UTF-16 through a fixed stack buffer and
    flushed whenever it fills, so writing never allocates and a log line of
    any length works. The flush threshold leaves room for a surrogate pair
    and the 0 that OutputDebugStringW needs.
*/

#if OS_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

constexpr u64 OS_WIN32_WIDE_CHUNK = 512;

// Where a chunk of UTF-16 goes: the debugger, or a console handle.
static void OsWin32FlushWide(u16 *buffer, u64 count, HANDLE console)
{
    if (count == 0)
    {
        return;
    }
    if (console != 0)
    {
        DWORD written = 0;
        WriteConsoleW(console, buffer, (DWORD)count, &written, 0);
    }
    else
    {
        buffer[count] = 0;
        OutputDebugStringW((wchar_t *)buffer);
    }
}

static void OsWin32WriteWide(String8 text, HANDLE console)
{
    u16 buffer[OS_WIN32_WIDE_CHUNK];
    u64 count = 0;
    u64 offset = 0;
    while (offset < text.size)
    {
        UnicodeDecode decode = Utf8Decode(text, offset);
        offset += decode.advance;
        count += Utf16Encode(buffer + count, decode.codepoint);
        if (count >= OS_WIN32_WIDE_CHUNK - 3)
        {
            OsWin32FlushWide(buffer, count, console);
            count = 0;
        }
    }
    OsWin32FlushWide(buffer, count, console);
}

u32 OsLastErrorCode(void)
{
    return GetLastError();
}

String8 OsErrorMessage(Arena *arena, u32 code)
{
    wchar_t buffer[512];
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD length =
      FormatMessageW(flags, 0, code, 0, buffer, (DWORD)(sizeof(buffer) / 2), 0);
    // Drop the trailing "\r\n" (and any other trailing whitespace).
    while (length > 0 && buffer[length - 1] <= ' ')
    {
        --length;
    }
    if (length == 0)
    {
        return Str8PushF(arena, "OS error %u (0x%08X)", code, code);
    }
    String16 wide = {};
    wide.data = (u16 *)buffer;
    wide.size = length;
    return Str8From16(arena, wide);
}

b32 OsDebuggerPresent(void)
{
    return IsDebuggerPresent() != 0;
}

void OsDebuggerWrite(String8 text)
{
    OsWin32WriteWide(text, 0);
}

#endif
