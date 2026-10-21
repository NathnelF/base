/*
    string8.h

    String8: a UTF-8 string as a pointer and a byte count. It replaces C's
    NUL-terminated char * and the string.h functions that go with it.

    What changes compared to C strings:
    - Length is stored, so it is O(1); nothing re-scans for a terminator.
    - Substrings are views (same bytes, new pointer and size), so slicing,
      trimming, splitting and searching never copy. Data may contain 0.
    - No function writes past an allocation. Sizes come from the arena or are
      clamped to the input, so there is no strcpy/strcat/sprintf overflow.
    - Concatenating many pieces goes through String8List and Str8Join, which
      allocates the total once and copies each piece once, instead of the
      O(n^2) re-scanning of repeated strcat.
    - Split returns views and leaves the input alone (no strtok state).
    - Parsing returns success and an out value instead of errno or a silent 0.
    - Character classes are ASCII on u8: no locale, no signed char UB.
    - The arena owns everything; there is no per-string free.

    Arena copies (Str8Copy, Str8Cat, Str8PushF, Str8Join, ...) always have a
    0 byte at data[size] that size does not count, so their data can go
    straight to C or OS APIs. Views (Str8Sub, Str8Trim, split pieces, ...) are
    NOT terminated; copy one first if an API needs a terminator.

    ZII: the zero String8 is the empty string, and every function accepts it.
    A failed allocation returns the empty string, never a string pointing at
    the arena's shared nil scratch.

    Search functions return the index of the match, or s.size when there is
    none. A miss passed straight into Str8Sub / Str8Skip gives an empty
    string, so most callers need no explicit check.

    Unicode: Utf8Decode / Utf16Decode return a codepoint and how many code
    units it used (0 at the end of the string). Invalid or truncated
    sequences decode to U+FFFD and advance by one unit. Str16From8 builds
    0-terminated UTF-16 for Win32 W functions.

    Formatting (string8_format.cpp): Str8PushF works like printf into the
    arena, plus %S for a String8 argument. See that file for the specifiers.

    NOTE: Every name here starts with Str8, Str16, String8, Utf8, Utf16,
    Char or STR8; the whole of base is one translation unit.
*/

#pragma once

#include <stdarg.h>

#include "../types/base_types.h"
#include "arena.h"

struct String8
{
    u8 *data;
    u64 size;
};

struct String16
{
    u16 *data;
    u64 size; // in u16 units
};

struct String8Node
{
    String8Node *next;
    String8 string;
};

struct String8List
{
    String8Node *first;
    String8Node *last;
    u64 count;
    u64 total_size; // sum of every node's string size
};

struct UnicodeDecode
{
    u32 codepoint;
    u32 advance; // code units consumed; 0 at the end of the input
};

constexpr u32 UNICODE_REPLACEMENT = 0xFFFD;

// A String8 view of a string literal, sized at compile time.
#define STR8_LIT(literal) (String8{(u8 *)(literal), sizeof(literal) - 1})

// Characters (ASCII only; bytes >= 0x80 are never letters, digits or space).
inline b32 CharIsSpace(u8 c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

inline b32 CharIsDigit(u8 c)
{
    return c >= '0' && c <= '9';
}

inline b32 CharIsUpper(u8 c)
{
    return c >= 'A' && c <= 'Z';
}

inline b32 CharIsLower(u8 c)
{
    return c >= 'a' && c <= 'z';
}

inline b32 CharIsAlpha(u8 c)
{
    return CharIsUpper(c) || CharIsLower(c);
}

inline b32 CharIsAlnum(u8 c)
{
    return CharIsAlpha(c) || CharIsDigit(c);
}

inline u8 CharToUpper(u8 c)
{
    if (CharIsLower(c))
    {
        return (u8)(c - ('a' - 'A'));
    }
    return c;
}

inline u8 CharToLower(u8 c)
{
    if (CharIsUpper(c))
    {
        return (u8)(c + ('a' - 'A'));
    }
    return c;
}

// Make
String8 Str8Make(u8 *data, u64 size);
String8 Str8FromCStr(const char *cstr);
String8 Str8Copy(Arena *arena, String8 s);

// Slice (views, clamped)
String8 Str8Sub(String8 s, u64 start, u64 end);
String8 Str8Prefix(String8 s, u64 count);
String8 Str8Skip(String8 s, u64 count);
String8 Str8Postfix(String8 s, u64 count);
String8 Str8Chop(String8 s, u64 count);

// Compare
b32 Str8Equal(String8 a, String8 b);
b32 Str8EqualNoCase(String8 a, String8 b);
i32 Str8Compare(String8 a, String8 b);
b32 Str8StartsWith(String8 s, String8 prefix);
b32 Str8EndsWith(String8 s, String8 postfix);

// Search (index of match, or s.size if none)
u64 Str8FindByte(String8 s, u8 byte, u64 start);
u64 Str8Find(String8 s, String8 needle, u64 start);
u64 Str8FindLast(String8 s, String8 needle);

// Trim (views)
String8 Str8TrimLeft(String8 s);
String8 Str8TrimRight(String8 s);
String8 Str8Trim(String8 s);

// Copy and transform
String8 Str8Cat(Arena *arena, String8 a, String8 b);
String8 Str8ToUpper(Arena *arena, String8 s);
String8 Str8ToLower(Arena *arena, String8 s);

// Lists
void Str8ListPush(Arena *arena, String8List *list, String8 s);
void Str8ListPushF(Arena *arena, String8List *list, const char *fmt, ...);
// Pieces between any of the bytes in delimiters. Pieces are views into s.
String8List Str8Split(Arena *arena,
                      String8 s,
                      String8 delimiters,
                      b32 keep_empty);
String8 Str8Join(Arena *arena, String8List *list, String8 separator);

// Parse. Decimal or 0x hex; I64 also takes a leading + or -. Return false
// (and set *out to 0) on empty input, bad characters or overflow.
b32 Str8ToU64(String8 s, u64 *out);
b32 Str8ToI64(String8 s, i64 *out);
// The whole string must be a number; no surrounding whitespace.
b32 Str8ToF64(String8 s, f64 *out);

// 64-bit FNV-1a. TODO: a faster hash (wyhash / rapidhash) for hash tables.
u64 Str8Hash(String8 s);

// Unicode
UnicodeDecode Utf8Decode(String8 s, u64 offset);
u32 Utf8Encode(u8 *out, u32 codepoint); // out needs 4 bytes; returns count
UnicodeDecode Utf16Decode(String16 s, u64 offset);
u32 Utf16Encode(u16 *out, u32 codepoint); // out needs 2 units; returns count
String16 Str16From8(Arena *arena, String8 s);
String8 Str8From16(Arena *arena, String16 s);

// Formatting (string8_format.cpp). PushFV consumes args.
String8 Str8PushF(Arena *arena, const char *fmt, ...);
String8 Str8PushFV(Arena *arena, const char *fmt, va_list args);
