/*
    string8.cpp

    Implementation of string8.h, except formatting (string8_format.cpp).

    Str8PushRaw is the one allocation path: it pushes size + 1 bytes, writes
    the trailing 0 and returns the empty string on failure. Everything that
    creates a string goes through it, which is what keeps the "arena copies
    are terminated" and "failure is empty" rules in one place.
    string8_format.cpp uses it too (same translation unit).

    Searching scans for the needle's first byte and confirms with MemEqual.
    TODO: SIMD byte scan.

    Str8Split marks delimiter bytes in a 256-bit table first, so each input
    byte costs one table lookup no matter how many delimiters there are.

    Str16From8 / Str8From16 allocate the worst case, convert in one pass,
    then pop the unused tail. That relies on the result being the last push
    on the arena, which holds because nothing else is pushed in between.

    NOTE: Str8ToF64 uses the CRT's strtod, which follows the C locale's
    decimal point. TODO: our own float parser.
*/

#include <stdlib.h>

#include "../types/base_memory.h"
#include "string8.h"

static String8 Str8PushRaw(Arena *arena, u64 size)
{
    String8 result = {};
    if (size == ~(u64)0)
    {
        return result;
    }
    u8 *data = ArenaPushArrayNoZero<u8>(arena, size + 1);
    if (ArenaPushFailed(data))
    {
        return result;
    }
    data[size] = 0;
    result.data = data;
    result.size = size;
    return result;
}

String8 Str8Make(u8 *data, u64 size)
{
    String8 result = {};
    if (data != 0)
    {
        result.data = data;
        result.size = size;
    }
    return result;
}

String8 Str8FromCStr(const char *cstr)
{
    String8 result = {};
    if (cstr == 0)
    {
        return result;
    }
    u64 size = 0;
    while (cstr[size] != 0)
    {
        ++size;
    }
    result.data = (u8 *)cstr;
    result.size = size;
    return result;
}

String8 Str8Copy(Arena *arena, String8 s)
{
    String8 result = Str8PushRaw(arena, s.size);
    MemCopy(result.data, s.data, result.size);
    return result;
}

String8 Str8Sub(String8 s, u64 start, u64 end)
{
    if (end > s.size)
    {
        end = s.size;
    }
    String8 result = {};
    if (start < end)
    {
        result.data = s.data + start;
        result.size = end - start;
    }
    return result;
}

String8 Str8Prefix(String8 s, u64 count)
{
    return Str8Sub(s, 0, count);
}

String8 Str8Skip(String8 s, u64 count)
{
    return Str8Sub(s, count, s.size);
}

String8 Str8Postfix(String8 s, u64 count)
{
    if (count > s.size)
    {
        count = s.size;
    }
    return Str8Sub(s, s.size - count, s.size);
}

String8 Str8Chop(String8 s, u64 count)
{
    if (count > s.size)
    {
        count = s.size;
    }
    return Str8Sub(s, 0, s.size - count);
}

b32 Str8Equal(String8 a, String8 b)
{
    return a.size == b.size && MemEqual(a.data, b.data, a.size);
}

b32 Str8EqualNoCase(String8 a, String8 b)
{
    if (a.size != b.size)
    {
        return 0;
    }
    for (u64 i = 0; i < a.size; ++i)
    {
        if (CharToLower(a.data[i]) != CharToLower(b.data[i]))
        {
            return 0;
        }
    }
    return 1;
}

i32 Str8Compare(String8 a, String8 b)
{
    u64 common = a.size;
    if (b.size < common)
    {
        common = b.size;
    }
    i32 result = MemCompare(a.data, b.data, common);
    if (result != 0)
    {
        return result;
    }
    if (a.size < b.size)
    {
        return -1;
    }
    if (a.size > b.size)
    {
        return 1;
    }
    return 0;
}

b32 Str8StartsWith(String8 s, String8 prefix)
{
    return prefix.size <= s.size && MemEqual(s.data, prefix.data, prefix.size);
}

b32 Str8EndsWith(String8 s, String8 postfix)
{
    if (postfix.size > s.size)
    {
        return 0;
    }
    return MemEqual(
      s.data + (s.size - postfix.size), postfix.data, postfix.size);
}

u64 Str8FindByte(String8 s, u8 byte, u64 start)
{
    for (u64 i = start; i < s.size; ++i)
    {
        if (s.data[i] == byte)
        {
            return i;
        }
    }
    return s.size;
}

u64 Str8Find(String8 s, String8 needle, u64 start)
{
    if (start > s.size)
    {
        return s.size;
    }
    if (needle.size == 0)
    {
        return start;
    }
    if (needle.size > s.size)
    {
        return s.size;
    }
    u64 last = s.size - needle.size;
    u8 first_byte = needle.data[0];
    for (u64 i = start; i <= last; ++i)
    {
        if (s.data[i] == first_byte &&
            MemEqual(s.data + i, needle.data, needle.size))
        {
            return i;
        }
    }
    return s.size;
}

u64 Str8FindLast(String8 s, String8 needle)
{
    if (needle.size == 0 || needle.size > s.size)
    {
        return s.size;
    }
    u8 first_byte = needle.data[0];
    for (u64 i = s.size - needle.size + 1; i > 0; --i)
    {
        u64 at = i - 1;
        if (s.data[at] == first_byte &&
            MemEqual(s.data + at, needle.data, needle.size))
        {
            return at;
        }
    }
    return s.size;
}

String8 Str8TrimLeft(String8 s)
{
    u64 start = 0;
    while (start < s.size && CharIsSpace(s.data[start]))
    {
        ++start;
    }
    return Str8Skip(s, start);
}

String8 Str8TrimRight(String8 s)
{
    u64 end = s.size;
    while (end > 0 && CharIsSpace(s.data[end - 1]))
    {
        --end;
    }
    return Str8Prefix(s, end);
}

String8 Str8Trim(String8 s)
{
    return Str8TrimRight(Str8TrimLeft(s));
}

String8 Str8Cat(Arena *arena, String8 a, String8 b)
{
    String8 result = {};
    if (b.size > ~(u64)0 - a.size)
    {
        return result;
    }
    result = Str8PushRaw(arena, a.size + b.size);
    if (result.data != 0)
    {
        MemCopy(result.data, a.data, a.size);
        MemCopy(result.data + a.size, b.data, b.size);
    }
    return result;
}

String8 Str8ToUpper(Arena *arena, String8 s)
{
    String8 result = Str8Copy(arena, s);
    for (u64 i = 0; i < result.size; ++i)
    {
        result.data[i] = CharToUpper(result.data[i]);
    }
    return result;
}

String8 Str8ToLower(Arena *arena, String8 s)
{
    String8 result = Str8Copy(arena, s);
    for (u64 i = 0; i < result.size; ++i)
    {
        result.data[i] = CharToLower(result.data[i]);
    }
    return result;
}

void Str8ListPush(Arena *arena, String8List *list, String8 s)
{
    if (list == 0)
    {
        return;
    }
    String8Node *node = ArenaPushStruct<String8Node>(arena);
    if (ArenaPushFailed(node))
    {
        return;
    }
    node->string = s;
    if (list->last != 0)
    {
        list->last->next = node;
    }
    else
    {
        list->first = node;
    }
    list->last = node;
    ++list->count;
    list->total_size += s.size;
}

void Str8ListPushF(Arena *arena, String8List *list, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    String8 s = Str8PushFV(arena, fmt, args);
    va_end(args);
    Str8ListPush(arena, list, s);
}

String8List Str8Split(Arena *arena,
                      String8 s,
                      String8 delimiters,
                      b32 keep_empty)
{
    String8List list = {};
    if (s.size == 0)
    {
        // An empty input has no pieces, even with keep_empty.
        return list;
    }
    u64 is_delimiter[4] = {};
    for (u64 i = 0; i < delimiters.size; ++i)
    {
        u8 byte = delimiters.data[i];
        is_delimiter[byte >> 6] |= 1ull << (byte & 63);
    }

    u64 piece_start = 0;
    for (u64 i = 0; i <= s.size; ++i)
    {
        b32 at_end = i == s.size;
        b32 split = at_end;
        if (!at_end)
        {
            u8 byte = s.data[i];
            split = (b32)((is_delimiter[byte >> 6] >> (byte & 63)) & 1);
        }
        if (split)
        {
            String8 piece = Str8Sub(s, piece_start, i);
            if (piece.size > 0 || keep_empty)
            {
                Str8ListPush(arena, &list, piece);
            }
            piece_start = i + 1;
        }
    }
    return list;
}

String8 Str8Join(Arena *arena, String8List *list, String8 separator)
{
    String8 result = {};
    if (list == 0)
    {
        return result;
    }
    u64 total = list->total_size;
    if (list->count > 1)
    {
        u64 gaps = list->count - 1;
        if (separator.size != 0 && gaps > (~(u64)0 - total) / separator.size)
        {
            return result;
        }
        total += gaps * separator.size;
    }
    result = Str8PushRaw(arena, total);
    if (result.data == 0)
    {
        return result;
    }
    u8 *at = result.data;
    for (String8Node *node = list->first; node != 0; node = node->next)
    {
        if (node != list->first)
        {
            MemCopy(at, separator.data, separator.size);
            at += separator.size;
        }
        MemCopy(at, node->string.data, node->string.size);
        at += node->string.size;
    }
    return result;
}

// Value of an ASCII digit in base 16, or 16 if it is not one.
static u32 Str8DigitValue(u8 c)
{
    if (CharIsDigit(c))
    {
        return (u32)(c - '0');
    }
    u8 lower = CharToLower(c);
    if (lower >= 'a' && lower <= 'f')
    {
        return (u32)(lower - 'a' + 10);
    }
    return 16;
}

b32 Str8ToU64(String8 s, u64 *out)
{
    u64 value = 0;
    if (out != 0)
    {
        *out = 0;
    }
    u64 base = 10;
    if (s.size >= 2 && s.data[0] == '0' && CharToLower(s.data[1]) == 'x')
    {
        base = 16;
        s = Str8Skip(s, 2);
    }
    if (s.size == 0)
    {
        return 0;
    }
    for (u64 i = 0; i < s.size; ++i)
    {
        u64 digit = Str8DigitValue(s.data[i]);
        if (digit >= base)
        {
            return 0;
        }
        if (value > (~(u64)0 - digit) / base)
        {
            return 0;
        }
        value = value * base + digit;
    }
    if (out != 0)
    {
        *out = value;
    }
    return 1;
}

b32 Str8ToI64(String8 s, i64 *out)
{
    if (out != 0)
    {
        *out = 0;
    }
    b32 negative = 0;
    if (s.size > 0 && (s.data[0] == '-' || s.data[0] == '+'))
    {
        negative = s.data[0] == '-';
        s = Str8Skip(s, 1);
    }
    u64 magnitude = 0;
    if (!Str8ToU64(s, &magnitude))
    {
        return 0;
    }
    u64 limit = 0x7FFFFFFFFFFFFFFFull;
    if (negative)
    {
        limit += 1;
    }
    if (magnitude > limit)
    {
        return 0;
    }
    if (out != 0)
    {
        if (negative)
        {
            *out = (i64)(0 - magnitude);
        }
        else
        {
            *out = (i64)magnitude;
        }
    }
    return 1;
}

b32 Str8ToF64(String8 s, f64 *out)
{
    if (out != 0)
    {
        *out = 0;
    }
    char buffer[64];
    if (s.size == 0 || s.size >= sizeof(buffer) || CharIsSpace(s.data[0]))
    {
        return 0;
    }
    MemCopy(buffer, s.data, s.size);
    buffer[s.size] = 0;
    char *end = 0;
    f64 value = strtod(buffer, &end);
    if (end != buffer + s.size)
    {
        return 0;
    }
    if (out != 0)
    {
        *out = value;
    }
    return 1;
}

u64 Str8Hash(String8 s)
{
    u64 hash = 0xCBF29CE484222325ull;
    for (u64 i = 0; i < s.size; ++i)
    {
        hash ^= s.data[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

UnicodeDecode Utf8Decode(String8 s, u64 offset)
{
    UnicodeDecode result = {};
    if (offset >= s.size)
    {
        return result;
    }
    result.codepoint = UNICODE_REPLACEMENT;
    result.advance = 1;

    u8 *at = s.data + offset;
    u64 remaining = s.size - offset;
    u8 lead = at[0];
    if (lead < 0x80)
    {
        result.codepoint = lead;
        return result;
    }

    u32 length = 0;
    u32 codepoint = 0;
    u32 minimum = 0;
    if ((lead & 0xE0) == 0xC0)
    {
        length = 2;
        codepoint = lead & 0x1F;
        minimum = 0x80;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        length = 3;
        codepoint = lead & 0x0F;
        minimum = 0x800;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        length = 4;
        codepoint = lead & 0x07;
        minimum = 0x10000;
    }
    else
    {
        return result;
    }
    if (remaining < length)
    {
        return result;
    }
    for (u32 i = 1; i < length; ++i)
    {
        if ((at[i] & 0xC0) != 0x80)
        {
            return result;
        }
        codepoint = (codepoint << 6) | (at[i] & 0x3F);
    }
    b32 surrogate = codepoint >= 0xD800 && codepoint <= 0xDFFF;
    if (codepoint < minimum || codepoint > 0x10FFFF || surrogate)
    {
        return result;
    }
    result.codepoint = codepoint;
    result.advance = length;
    return result;
}

u32 Utf8Encode(u8 *out, u32 codepoint)
{
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
    {
        codepoint = UNICODE_REPLACEMENT;
    }
    if (codepoint < 0x80)
    {
        out[0] = (u8)codepoint;
        return 1;
    }
    if (codepoint < 0x800)
    {
        out[0] = (u8)(0xC0 | (codepoint >> 6));
        out[1] = (u8)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000)
    {
        out[0] = (u8)(0xE0 | (codepoint >> 12));
        out[1] = (u8)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (u8)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    out[0] = (u8)(0xF0 | (codepoint >> 18));
    out[1] = (u8)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (u8)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (u8)(0x80 | (codepoint & 0x3F));
    return 4;
}

UnicodeDecode Utf16Decode(String16 s, u64 offset)
{
    UnicodeDecode result = {};
    if (offset >= s.size)
    {
        return result;
    }
    result.codepoint = UNICODE_REPLACEMENT;
    result.advance = 1;
    u32 unit = s.data[offset];
    if (unit < 0xD800 || unit > 0xDFFF)
    {
        result.codepoint = unit;
        return result;
    }
    if (unit <= 0xDBFF && offset + 1 < s.size)
    {
        u32 low = s.data[offset + 1];
        if (low >= 0xDC00 && low <= 0xDFFF)
        {
            result.codepoint =
              0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
            result.advance = 2;
        }
    }
    return result;
}

u32 Utf16Encode(u16 *out, u32 codepoint)
{
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
    {
        codepoint = UNICODE_REPLACEMENT;
    }
    if (codepoint < 0x10000)
    {
        out[0] = (u16)codepoint;
        return 1;
    }
    codepoint -= 0x10000;
    out[0] = (u16)(0xD800 + (codepoint >> 10));
    out[1] = (u16)(0xDC00 + (codepoint & 0x3FF));
    return 2;
}

String16 Str16From8(Arena *arena, String8 s)
{
    // Every UTF-8 sequence produces at most one UTF-16 unit per byte.
    String16 result = {};
    if (s.size == ~(u64)0)
    {
        return result;
    }
    u64 capacity = s.size + 1;
    u16 *data = ArenaPushArrayNoZero<u16>(arena, capacity);
    if (ArenaPushFailed(data))
    {
        return result;
    }
    u64 size = 0;
    u64 offset = 0;
    while (offset < s.size)
    {
        UnicodeDecode decode = Utf8Decode(s, offset);
        size += Utf16Encode(data + size, decode.codepoint);
        offset += decode.advance;
    }
    data[size] = 0;
    ArenaPop(arena, (capacity - size - 1) * sizeof(u16));
    result.data = data;
    result.size = size;
    return result;
}

String8 Str8From16(Arena *arena, String16 s)
{
    // Every UTF-16 unit produces at most three UTF-8 bytes (a surrogate pair
    // is two units and four bytes).
    String8 result = {};
    if (s.size > (~(u64)0 - 1) / 3)
    {
        return result;
    }
    u64 capacity = s.size * 3;
    result = Str8PushRaw(arena, capacity);
    if (result.data == 0)
    {
        return result;
    }
    u64 size = 0;
    u64 offset = 0;
    while (offset < s.size)
    {
        UnicodeDecode decode = Utf16Decode(s, offset);
        size += Utf8Encode(result.data + size, decode.codepoint);
        offset += decode.advance;
    }
    result.data[size] = 0;
    ArenaPop(arena, capacity - size);
    result.size = size;
    return result;
}
