/*
    os_console.h

    The standard streams: stdout, stderr and stdin, plus Print, a
    printf-style write to stdout.

    Output takes UTF-8. When the stream is a console it is written with
    WriteConsoleW (correct Unicode regardless of the console code page);
    when it is redirected to a file or pipe the UTF-8 bytes are written
    as-is. OsStdOutWrite and OsStdErrWrite do no allocation.

    Print / PrintV use the same specifiers as Str8PushF (%S for String8, %s
    for C strings, ...; see containers/string8_format.cpp). Each call
    formats into a per-thread scratch arena and writes once; nothing is
    buffered across calls.
    TODO: buffered stdout with an explicit flush if Print shows up in
    profiles.

    OsStdInReadLine reads one line into the arena, without its "\n" or
    "\r\n". At end of input it returns an empty string and sets *out_eof
    (out_eof may be null). A last line with no newline is returned normally
    and the end is reported on the next call, so
        b32 eof = 0;
        for (String8 line = OsStdInReadLine(arena, &eof); !eof;
             line = OsStdInReadLine(arena, &eof))
    sees every line. Redirected input skips a leading UTF-8 BOM. In a
    console, a line starting with Ctrl+Z, or Ctrl+C during the read, is end
    of input.

    NOTE: A read failure is reported as end of input. It is not logged: the
    os layer sits below log.

    NOTE: Reading stdin is not thread-safe. Redirected input is buffered in
    one process-wide buffer, since stdin is one process-wide stream.

    This file is part of the upper os layer: it needs String8 and Arena, so
    it is included after containers (see base.cpp).

    TODO: POSIX implementation (write(2) / read(2)).
*/

#pragma once

#include <stdarg.h>

#include "../architecture/arch_context.h"
#include "../containers/arena.h"
#include "../containers/string8.h"
#include "../types/base_types.h"

void OsStdOutWrite(String8 text);
void OsStdErrWrite(String8 text);
String8 OsStdInReadLine(Arena *arena, b32 *out_eof);

void Print(const char *fmt, ...);
void PrintV(const char *fmt, va_list args);
