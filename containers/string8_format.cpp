/*
    string8_format.cpp

    Str8PushF / Str8PushFV: printf-style formatting into an arena.

    The format string is run twice over copies of the arguments: the first
    pass only counts bytes, the second writes into one exact-size allocation
    from Str8PushRaw (so the result is 0-terminated like every arena string).
    The writer bounds-checks every byte, so a mismatch between the two passes
    can truncate but never overflow.

    Specifiers:
        %S          String8 (passed by value)
        %s          const char *; null prints "(null)"
        %c          character (int)
        %d %i       signed integer
        %u          unsigned integer
        %x %X       unsigned hex, lower / upper case
        %p          pointer as 0x + 16 hex digits
        %f %e %g    double (also %F %E %G), see NOTE
        %%          a literal %
    Length:  l (long), ll (long long), z (size_t); none means int.
    Flags:   - (left align), 0 (zero pad numbers), width (digits or *),
             .precision (digits or *): max bytes for %s / %S, digits for
             floats.

    An unknown specifier, or a format that ends in the middle of one, is a
    typo in the format string: it ASSERTs (once, in the counting pass), then
    is copied to the output as written and consumes no argument. Passing
   arguments that do not match the specifiers is the caller's bug, exactly as
   with printf.

    NOTE: Floats go through the CRT's snprintf one specifier at a time, so
    they follow the C locale's decimal point. Float precision is capped at 64
    digits. TODO: our own float printer (Ryu style).

    NOTE: Width and precision are capped at 16M so a typo cannot ask for a
    gigantic allocation.
*/

#include <stdio.h>

#include "../types/base_assert.h"
#include "../types/base_memory.h"
#include "string8.h"

constexpr u64 STR8_FORMAT_MAX_WIDTH = 1 << 24;
constexpr u64 STR8_FORMAT_MAX_FLOAT_PRECISION = 64;

struct Str8FormatWriter
{
    u8 *buffer; // null in the counting pass
    u64 capacity;
    u64 size; // bytes produced so far, even past capacity
};

static void Str8FormatPut(Str8FormatWriter *writer, u8 byte)
{
    if (writer->size < writer->capacity)
    {
        writer->buffer[writer->size] = byte;
    }
    ++writer->size;
}

static void Str8FormatPutString(Str8FormatWriter *writer, String8 s)
{
    for (u64 i = 0; i < s.size; ++i)
    {
        Str8FormatPut(writer, s.data[i]);
    }
}

static void Str8FormatPutRepeat(Str8FormatWriter *writer, u8 byte, u64 count)
{
    for (u64 i = 0; i < count; ++i)
    {
        Str8FormatPut(writer, byte);
    }
}

// Writes sign + body padded to width. Zero padding goes between the sign and
// the body, like printf's "-0042".
static void Str8FormatPutPadded(Str8FormatWriter *writer,
                                String8 sign,
                                String8 body,
                                u64 width,
                                b32 left,
                                b32 zero)
{
    u64 length = sign.size + body.size;
    u64 pad = 0;
    if (width > length)
    {
        pad = width - length;
    }
    if (left)
    {
        Str8FormatPutString(writer, sign);
        Str8FormatPutString(writer, body);
        Str8FormatPutRepeat(writer, ' ', pad);
    }
    else if (zero)
    {
        Str8FormatPutString(writer, sign);
        Str8FormatPutRepeat(writer, '0', pad);
        Str8FormatPutString(writer, body);
    }
    else
    {
        Str8FormatPutRepeat(writer, ' ', pad);
        Str8FormatPutString(writer, sign);
        Str8FormatPutString(writer, body);
    }
}

// Digits of value in base, written backwards from the end of buffer.
static String8 Str8FormatDigits(u8 *buffer,
                                u64 buffer_size,
                                u64 value,
                                u64 base,
                                b32 upper)
{
    const char *digits = "0123456789abcdef";
    if (upper)
    {
        digits = "0123456789ABCDEF";
    }
    u8 *at = buffer + buffer_size;
    do
    {
        --at;
        *at = (u8)digits[value % base];
        value /= base;
    } while (value != 0);
    return Str8Make(at, (u64)(buffer + buffer_size - at));
}

static u64 Str8FormatParseNumber(const u8 **at)
{
    u64 value = 0;
    while (CharIsDigit(**at))
    {
        if (value < STR8_FORMAT_MAX_WIDTH)
        {
            value = value * 10 + (u64)(**at - '0');
        }
        ++*at;
    }
    if (value > STR8_FORMAT_MAX_WIDTH)
    {
        value = STR8_FORMAT_MAX_WIDTH;
    }
    return value;
}

static void Str8FormatRun(Str8FormatWriter *writer,
                          const char *fmt,
                          va_list args)
{
    const u8 *at = (const u8 *)fmt;
    while (*at != 0)
    {
        if (*at != '%')
        {
            Str8FormatPut(writer, *at);
            ++at;
            continue;
        }
        const u8 *spec_start = at;
        ++at;

        b32 left = 0;
        b32 zero = 0;
        for (;;)
        {
            if (*at == '-')
            {
                left = 1;
            }
            else if (*at == '0')
            {
                zero = 1;
            }
            else
            {
                break;
            }
            ++at;
        }

        u64 width = 0;
        if (*at == '*')
        {
            i64 value = va_arg(args, int);
            if (value < 0)
            {
                left = 1;
                value = -value;
            }
            width = (u64)value;
            if (width > STR8_FORMAT_MAX_WIDTH)
            {
                width = STR8_FORMAT_MAX_WIDTH;
            }
            ++at;
        }
        else
        {
            width = Str8FormatParseNumber(&at);
        }

        b32 has_precision = 0;
        u64 precision = 0;
        if (*at == '.')
        {
            ++at;
            has_precision = 1;
            if (*at == '*')
            {
                int value = va_arg(args, int);
                if (value < 0)
                {
                    has_precision = 0;
                }
                else
                {
                    precision = (u64)value;
                }
                ++at;
            }
            else
            {
                precision = Str8FormatParseNumber(&at);
            }
        }

        // 0 = int, 1 = long, 2 = long long, 3 = size_t
        int length = 0;
        if (*at == 'l')
        {
            ++at;
            length = 1;
            if (*at == 'l')
            {
                ++at;
                length = 2;
            }
        }
        else if (*at == 'z')
        {
            ++at;
            length = 3;
        }

        u8 conversion = *at;
        // Typos assert in the counting pass only, so once per bad specifier.
        b32 counting_pass = writer->buffer == 0;
        if (conversion == 0)
        {
            // Format ended mid-specifier: copy what was there.
            if (counting_pass)
            {
                ASSERT_FAILED_MSG(
                  "Str8PushF: format string ends inside a specifier");
            }
            Str8FormatPutString(
              writer, Str8Make((u8 *)spec_start, (u64)(at - spec_start)));
            break;
        }
        ++at;

        u8 digit_buffer[32];
        String8 no_sign = {};
        switch (conversion)
        {
            case 'd':
            case 'i':
            {
                i64 value = 0;
                switch (length)
                {
                    case 0:
                        value = va_arg(args, int);
                        break;
                    case 1:
                        value = va_arg(args, long);
                        break;
                    case 2:
                        value = va_arg(args, long long);
                        break;
                    default:
                        value = (i64)va_arg(args, size_t);
                        break;
                }
                String8 sign = no_sign;
                u64 magnitude = (u64)value;
                if (value < 0)
                {
                    sign = STR8_LIT("-");
                    magnitude = 0 - (u64)value;
                }
                String8 body = Str8FormatDigits(
                  digit_buffer, sizeof(digit_buffer), magnitude, 10, 0);
                Str8FormatPutPadded(writer, sign, body, width, left, zero);
            }
            break;

            case 'u':
            case 'x':
            case 'X':
            {
                u64 value = 0;
                switch (length)
                {
                    case 0:
                        value = va_arg(args, unsigned int);
                        break;
                    case 1:
                        value = va_arg(args, unsigned long);
                        break;
                    case 2:
                        value = va_arg(args, unsigned long long);
                        break;
                    default:
                        value = va_arg(args, size_t);
                        break;
                }
                u64 base = 16;
                if (conversion == 'u')
                {
                    base = 10;
                }
                String8 body = Str8FormatDigits(digit_buffer,
                                                sizeof(digit_buffer),
                                                value,
                                                base,
                                                conversion == 'X');
                Str8FormatPutPadded(writer, no_sign, body, width, left, zero);
            }
            break;

            case 'p':
            {
                u64 value = (u64)(uptr)va_arg(args, void *);
                String8 digits = Str8FormatDigits(
                  digit_buffer, sizeof(digit_buffer), value, 16, 0);
                u8 *start = digit_buffer + sizeof(digit_buffer) - 16;
                MemSet(start, '0', 16 - digits.size);
                String8 body = Str8Make(start, 16);
                Str8FormatPutPadded(
                  writer, STR8_LIT("0x"), body, width, left, 0);
            }
            break;

            case 'c':
            {
                u8 byte = (u8)va_arg(args, int);
                Str8FormatPutPadded(
                  writer, no_sign, Str8Make(&byte, 1), width, left, 0);
            }
            break;

            case 's':
            {
                const char *cstr = va_arg(args, const char *);
                String8 body = STR8_LIT("(null)");
                if (cstr != 0)
                {
                    u64 size = 0;
                    while (cstr[size] != 0 &&
                           (!has_precision || size < precision))
                    {
                        ++size;
                    }
                    body = Str8Make((u8 *)cstr, size);
                }
                Str8FormatPutPadded(writer, no_sign, body, width, left, 0);
            }
            break;

            case 'S':
            {
                String8 body = va_arg(args, String8);
                if (has_precision)
                {
                    body = Str8Prefix(body, precision);
                }
                Str8FormatPutPadded(writer, no_sign, body, width, left, 0);
            }
            break;

            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            {
                f64 value = va_arg(args, double);
                char spec[16];
                int spec_size = 0;
                spec[spec_size++] = '%';
                if (has_precision)
                {
                    if (precision > STR8_FORMAT_MAX_FLOAT_PRECISION)
                    {
                        precision = STR8_FORMAT_MAX_FLOAT_PRECISION;
                    }
                    spec[spec_size++] = '.';
                    if (precision >= 10)
                    {
                        spec[spec_size++] = (char)('0' + precision / 10);
                    }
                    spec[spec_size++] = (char)('0' + precision % 10);
                }
                spec[spec_size++] = (char)conversion;
                spec[spec_size] = 0;

                // Max: 309 integer digits + '.' + 64 decimals + sign.
                char text[512];
                int count = snprintf(text, sizeof(text), spec, value);
                if (count < 0)
                {
                    count = 0;
                }
                if (count >= (int)sizeof(text))
                {
                    count = (int)sizeof(text) - 1;
                }
                String8 body = Str8Make((u8 *)text, (u64)count);
                String8 sign = no_sign;
                if (body.size > 0 && body.data[0] == '-')
                {
                    sign = Str8Prefix(body, 1);
                    body = Str8Skip(body, 1);
                }
                Str8FormatPutPadded(writer, sign, body, width, left, zero);
            }
            break;

            case '%':
            {
                Str8FormatPut(writer, '%');
            }
            break;

            default:
            {
                if (counting_pass)
                {
                    ASSERT_FAILED_MSG("Str8PushF: unknown format specifier");
                }
                Str8FormatPutString(
                  writer, Str8Make((u8 *)spec_start, (u64)(at - spec_start)));
            }
            break;
        }
    }
}

String8 Str8PushFV(Arena *arena, const char *fmt, va_list args)
{
    if (fmt == 0)
    {
        fmt = "";
    }
    va_list count_args;
    va_copy(count_args, args);
    Str8FormatWriter counter = {};
    Str8FormatRun(&counter, fmt, count_args);
    va_end(count_args);

    String8 result = Str8PushRaw(arena, counter.size);
    if (result.data == 0)
    {
        return result;
    }
    Str8FormatWriter writer = {};
    writer.buffer = result.data;
    writer.capacity = result.size;
    Str8FormatRun(&writer, fmt, args);
    return result;
}

String8 Str8PushF(Arena *arena, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    String8 result = Str8PushFV(arena, fmt, args);
    va_end(args);
    return result;
}
