/*
    string_test.cpp

    Exercises String8: construction and termination, slicing, comparison,
    search, trimming, lists (split / join), parsing, hashing, formatting,
    UTF-8 / UTF-16 conversion, and failure on the nil arena. Prints FAIL
    lines and returns nonzero if anything failed. Build with
    `fbuild string-test-debug` (or -release / -profile) from the repo root.

    NOTE: Uses printf until the os layer has its own output functions.
*/

#include <stdio.h>

#include "../../architecture/architecture.cpp"
#include "../../os/os.cpp"
#include "../containers.cpp"

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

// Checks a string against a literal and prints both on mismatch.
#define CHECK_STR(actual, expected)                                            \
    do                                                                         \
    {                                                                          \
        String8 check_actual = (actual);                                       \
        String8 check_expected = STR8_LIT(expected);                           \
        if (!Str8Equal(check_actual, check_expected))                          \
        {                                                                      \
            printf("FAIL line %d: \"%.*s\" != \"%s\"\n",                        \
                   __LINE__,                                                   \
                   (int)check_actual.size,                                     \
                   (const char *)check_actual.data,                            \
                   expected);                                                  \
            ++test_failures;                                                   \
        }                                                                      \
    } while (0)

static void TestMake(Arena *arena)
{
    String8 hello = STR8_LIT("hello");
    CHECK(hello.size == 5);
    CHECK(Str8FromCStr(0).size == 0 && Str8FromCStr(0).data == 0);
    CHECK(Str8FromCStr("abc").size == 3);
    CHECK(Str8Make(0, 10).size == 0);

    String8 copy = Str8Copy(arena, hello);
    CHECK_STR(copy, "hello");
    CHECK(copy.data != hello.data);
    CHECK(copy.data[copy.size] == 0);

    // A copied view is terminated even though the view was not.
    String8 view = Str8Prefix(STR8_LIT("hello world"), 5);
    String8 view_copy = Str8Copy(arena, view);
    CHECK(view_copy.data[5] == 0);

    // Embedded zeros are just bytes.
    String8 with_zero = STR8_LIT("a\0b");
    CHECK(with_zero.size == 3);
    CHECK(Str8Copy(arena, with_zero).size == 3);
}

static void TestSlice(void)
{
    String8 s = STR8_LIT("hello");
    CHECK_STR(Str8Sub(s, 1, 4), "ell");
    CHECK_STR(Str8Sub(s, 3, 100), "lo");
    CHECK(Str8Sub(s, 4, 2).size == 0);
    CHECK(Str8Sub(s, 10, 20).size == 0);
    CHECK_STR(Str8Prefix(s, 2), "he");
    CHECK_STR(Str8Prefix(s, 50), "hello");
    CHECK_STR(Str8Skip(s, 3), "lo");
    CHECK(Str8Skip(s, 50).size == 0);
    CHECK_STR(Str8Postfix(s, 3), "llo");
    CHECK_STR(Str8Postfix(s, 50), "hello");
    CHECK_STR(Str8Chop(s, 2), "hel");
    CHECK(Str8Chop(s, 50).size == 0);

    String8 empty = {};
    CHECK(Str8Sub(empty, 0, 5).size == 0);
    CHECK(Str8Postfix(empty, 5).size == 0);
}

static void TestCompare(void)
{
    String8 empty = {};
    CHECK(Str8Equal(STR8_LIT("abc"), STR8_LIT("abc")));
    CHECK(!Str8Equal(STR8_LIT("abc"), STR8_LIT("abd")));
    CHECK(!Str8Equal(STR8_LIT("abc"), STR8_LIT("ab")));
    CHECK(Str8Equal(empty, STR8_LIT("")));
    CHECK(Str8EqualNoCase(STR8_LIT("HeLLo"), STR8_LIT("hello")));
    CHECK(!Str8EqualNoCase(STR8_LIT("hello"), STR8_LIT("help!")));

    CHECK(Str8Compare(STR8_LIT("abc"), STR8_LIT("abd")) == -1);
    CHECK(Str8Compare(STR8_LIT("abd"), STR8_LIT("abc")) == 1);
    CHECK(Str8Compare(STR8_LIT("abc"), STR8_LIT("ab")) == 1);
    CHECK(Str8Compare(STR8_LIT("ab"), STR8_LIT("abc")) == -1);
    CHECK(Str8Compare(STR8_LIT("abc"), STR8_LIT("abc")) == 0);
    CHECK(Str8Compare(empty, STR8_LIT("a")) == -1);
    // Bytes compare unsigned: 0xFF sorts after 'a'.
    CHECK(Str8Compare(STR8_LIT("\xFF"), STR8_LIT("a")) == 1);

    CHECK(Str8StartsWith(STR8_LIT("hello"), STR8_LIT("he")));
    CHECK(Str8StartsWith(STR8_LIT("hello"), empty));
    CHECK(!Str8StartsWith(STR8_LIT("he"), STR8_LIT("hello")));
    CHECK(Str8EndsWith(STR8_LIT("hello"), STR8_LIT("llo")));
    CHECK(!Str8EndsWith(STR8_LIT("hello"), STR8_LIT("hel")));
    CHECK(!Str8EndsWith(STR8_LIT("lo"), STR8_LIT("hello")));
}

static void TestSearch(void)
{
    String8 s = STR8_LIT("hello world");
    CHECK(Str8FindByte(s, 'o', 0) == 4);
    CHECK(Str8FindByte(s, 'o', 5) == 7);
    CHECK(Str8FindByte(s, 'z', 0) == s.size);
    CHECK(Str8FindByte(s, 'h', 100) == s.size);

    CHECK(Str8Find(s, STR8_LIT("o"), 0) == 4);
    CHECK(Str8Find(s, STR8_LIT("o"), 5) == 7);
    CHECK(Str8Find(s, STR8_LIT("world"), 0) == 6);
    CHECK(Str8Find(s, STR8_LIT("worlds"), 0) == s.size);
    CHECK(Str8Find(s, STR8_LIT("xyz"), 0) == s.size);
    CHECK(Str8Find(STR8_LIT("ab"), STR8_LIT("abc"), 0) == 2);
    CHECK(Str8Find(s, STR8_LIT(""), 3) == 3);
    CHECK(Str8Find(s, STR8_LIT("d"), 10) == 10);
    CHECK(Str8Find(s, STR8_LIT("o"), 100) == s.size);

    CHECK(Str8FindLast(s, STR8_LIT("o")) == 7);
    CHECK(Str8FindLast(s, STR8_LIT("hello")) == 0);
    CHECK(Str8FindLast(s, STR8_LIT("q")) == s.size);
    CHECK(Str8FindLast(s, STR8_LIT("")) == s.size);

    // A miss flows straight into slicing as an empty string.
    CHECK(Str8Skip(s, Str8Find(s, STR8_LIT("xyz"), 0)).size == 0);
    CHECK_STR(Str8Skip(s, Str8Find(s, STR8_LIT("wor"), 0)), "world");
}

static void TestTrim(void)
{
    CHECK_STR(Str8Trim(STR8_LIT("  hi there \n\t")), "hi there");
    CHECK_STR(Str8TrimLeft(STR8_LIT("  hi ")), "hi ");
    CHECK_STR(Str8TrimRight(STR8_LIT("  hi ")), "  hi");
    CHECK(Str8Trim(STR8_LIT(" \r\n\t\v\f ")).size == 0);
    String8 empty = {};
    CHECK(Str8Trim(empty).size == 0);
}

static void TestTransform(Arena *arena)
{
    String8 cat = Str8Cat(arena, STR8_LIT("foo"), STR8_LIT("bar"));
    CHECK_STR(cat, "foobar");
    CHECK(cat.data[6] == 0);
    String8 empty = {};
    CHECK_STR(Str8Cat(arena, empty, STR8_LIT("x")), "x");
    CHECK(Str8Cat(arena, empty, empty).size == 0);

    CHECK_STR(Str8ToUpper(arena, STR8_LIT("abC1-z")), "ABC1-Z");
    CHECK_STR(Str8ToLower(arena, STR8_LIT("ABc1-Z")), "abc1-z");
    // Non-ASCII bytes are left alone.
    CHECK_STR(Str8ToUpper(arena, STR8_LIT("\xC3\xA9")), "\xC3\xA9");

    CHECK(CharIsSpace(' ') && CharIsSpace('\n') && !CharIsSpace('a'));
    CHECK(CharIsDigit('7') && !CharIsDigit('a'));
    CHECK(CharIsAlpha('q') && CharIsAlpha('Q') && !CharIsAlpha('1'));
    CHECK(CharIsAlnum('1') && !CharIsAlnum('_'));
    CHECK(!CharIsAlpha(0xE9));
}

static void TestLists(Arena *arena)
{
    String8List parts = Str8Split(arena, STR8_LIT("a,b,,c"), STR8_LIT(","), 0);
    CHECK(parts.count == 3);
    CHECK_STR(Str8Join(arena, &parts, STR8_LIT("-")), "a-b-c");

    String8List with_empty =
      Str8Split(arena, STR8_LIT("a,b,,c,"), STR8_LIT(","), 1);
    CHECK(with_empty.count == 5);
    CHECK_STR(Str8Join(arena, &with_empty, STR8_LIT(",")), "a,b,,c,");

    String8List words =
      Str8Split(arena, STR8_LIT("  one two\tthree\n"), STR8_LIT(" \t\n"), 0);
    CHECK(words.count == 3);
    CHECK_STR(words.first->string, "one");
    CHECK_STR(words.last->string, "three");
    CHECK(words.total_size == 11);

    // Pieces are views into the input, not copies.
    String8 input = STR8_LIT("x;y");
    String8List views = Str8Split(arena, input, STR8_LIT(";"), 0);
    CHECK(views.first->string.data == input.data);

    String8 empty = {};
    CHECK(Str8Split(arena, empty, STR8_LIT(","), 1).count == 0);
    CHECK(Str8Split(arena, STR8_LIT("abc"), empty, 0).count == 1);

    String8List built = {};
    Str8ListPush(arena, &built, STR8_LIT("x"));
    Str8ListPushF(arena, &built, "%d", 42);
    Str8ListPush(arena, &built, STR8_LIT("y"));
    CHECK(built.count == 3);
    CHECK_STR(Str8Join(arena, &built, STR8_LIT(", ")), "x, 42, y");
    CHECK_STR(Str8Join(arena, &built, empty), "x42y");

    String8List none = {};
    String8 joined = Str8Join(arena, &none, STR8_LIT(","));
    CHECK(joined.size == 0);
    CHECK(Str8Join(arena, 0, STR8_LIT(",")).size == 0);
}

static void TestParse(void)
{
    u64 u = 99;
    CHECK(Str8ToU64(STR8_LIT("123"), &u) && u == 123);
    CHECK(Str8ToU64(STR8_LIT("0"), &u) && u == 0);
    CHECK(Str8ToU64(STR8_LIT("0x1F"), &u) && u == 31);
    CHECK(Str8ToU64(STR8_LIT("0XfF"), &u) && u == 255);
    CHECK(Str8ToU64(STR8_LIT("18446744073709551615"), &u) && u == ~0ull);
    CHECK(Str8ToU64(STR8_LIT("0xFFFFFFFFFFFFFFFF"), &u) && u == ~0ull);
    CHECK(!Str8ToU64(STR8_LIT("18446744073709551616"), &u) && u == 0);
    CHECK(!Str8ToU64(STR8_LIT("0x10000000000000000"), &u));
    CHECK(!Str8ToU64(STR8_LIT(""), &u));
    CHECK(!Str8ToU64(STR8_LIT("0x"), &u));
    CHECK(!Str8ToU64(STR8_LIT("12a"), &u));
    CHECK(!Str8ToU64(STR8_LIT(" 12"), &u));
    CHECK(!Str8ToU64(STR8_LIT("-1"), &u));
    CHECK(!Str8ToU64(STR8_LIT("ff"), &u));

    i64 i = 99;
    CHECK(Str8ToI64(STR8_LIT("-42"), &i) && i == -42);
    CHECK(Str8ToI64(STR8_LIT("+42"), &i) && i == 42);
    CHECK(Str8ToI64(STR8_LIT("-0x10"), &i) && i == -16);
    CHECK(Str8ToI64(STR8_LIT("9223372036854775807"), &i) &&
          i == 0x7FFFFFFFFFFFFFFFll);
    CHECK(Str8ToI64(STR8_LIT("-9223372036854775808"), &i) &&
          i == (i64)0x8000000000000000ull);
    CHECK(!Str8ToI64(STR8_LIT("9223372036854775808"), &i) && i == 0);
    CHECK(!Str8ToI64(STR8_LIT("-9223372036854775809"), &i));
    CHECK(!Str8ToI64(STR8_LIT("-"), &i));
    CHECK(!Str8ToI64(STR8_LIT("--5"), &i));
    CHECK(!Str8ToI64(STR8_LIT("+-5"), &i));

    f64 f = 99;
    CHECK(Str8ToF64(STR8_LIT("3.5"), &f) && f == 3.5);
    CHECK(Str8ToF64(STR8_LIT("-0.25"), &f) && f == -0.25);
    CHECK(Str8ToF64(STR8_LIT("1e3"), &f) && f == 1000.0);
    CHECK(!Str8ToF64(STR8_LIT(" 3.5"), &f) && f == 0);
    CHECK(!Str8ToF64(STR8_LIT("3.5x"), &f));
    CHECK(!Str8ToF64(STR8_LIT(""), &f));
    CHECK(!Str8ToF64(STR8_LIT("abc"), &f));

    // Parsing a view stops at the view's end, not at a terminator.
    String8 view = Str8Prefix(STR8_LIT("12345"), 3);
    CHECK(Str8ToU64(view, &u) && u == 123);
    CHECK(Str8ToF64(Str8Prefix(STR8_LIT("2.50000"), 3), &f) && f == 2.5);

    // Null out pointers are allowed (just validates).
    CHECK(Str8ToU64(STR8_LIT("7"), 0));
}

static void TestHash(void)
{
    String8 empty = {};
    CHECK(Str8Hash(empty) == 0xCBF29CE484222325ull);
    CHECK(Str8Hash(STR8_LIT("a")) == 0xAF63DC4C8601EC8Cull);
    CHECK(Str8Hash(STR8_LIT("hello")) ==
          Str8Hash(Str8Prefix(STR8_LIT("hello world"), 5)));
    CHECK(Str8Hash(STR8_LIT("hello")) != Str8Hash(STR8_LIT("hellp")));
}

static void TestFormat(Arena *arena)
{
    CHECK_STR(Str8PushF(arena, "plain"), "plain");
    CHECK_STR(Str8PushF(arena, ""), "");
    CHECK(Str8PushF(arena, "").data != 0);
    CHECK_STR(Str8PushF(arena, "%d %i %u", -7, 8, 9u), "-7 8 9");
    CHECK_STR(Str8PushF(arena, "%d|%5d|%-5d|%05d", -42, 42, 42, -42),
              "-42|   42|42   |-0042");
    CHECK_STR(Str8PushF(arena, "%*d|%-*d", 4, 1, 4, 2), "   1|2   ");
    CHECK_STR(Str8PushF(arena, "%x %X %04x", 255u, 255u, 10u), "ff FF 000a");
    CHECK_STR(Str8PushF(arena, "%llx", 0xDEADBEEFCAFEull), "deadbeefcafe");
    CHECK_STR(Str8PushF(arena, "%llu", ~0ull), "18446744073709551615");
    CHECK_STR(Str8PushF(arena, "%lld", (long long)0x8000000000000000ull),
              "-9223372036854775808");
    CHECK_STR(Str8PushF(arena, "%zu", (size_t)12345), "12345");
    CHECK_STR(Str8PushF(arena, "%d", -2147483647 - 1), "-2147483648");
    CHECK_STR(Str8PushF(arena, "%c%c%3c", 'a', 'b', 'c'), "ab  c");
    CHECK_STR(Str8PushF(arena, "%s|%s", "hi", (const char *)0), "hi|(null)");
    CHECK_STR(Str8PushF(arena, "%.2s|%5s|%-5s|", "hello", "ab", "ab"),
              "he|   ab|ab   |");
    CHECK_STR(Str8PushF(arena, "%p", (void *)0x1234), "0x0000000000001234");
    CHECK_STR(Str8PushF(arena, "100%%"), "100%");
    CHECK_STR(Str8PushF(arena, "%q %d", 5), "%q 5");
    CHECK_STR(Str8PushF(arena, "end%"), "end%");

    String8 name = STR8_LIT("world");
    String8 view = Str8Prefix(STR8_LIT("abcdef"), 3);
    CHECK_STR(Str8PushF(arena, "hello %S!", name), "hello world!");
    CHECK_STR(Str8PushF(arena, "[%S]", view), "[abc]");
    CHECK_STR(Str8PushF(arena, "[%7S][%-7S]", name, name),
              "[  world][world  ]");
    CHECK_STR(Str8PushF(arena, "[%.*S]", 2, name), "[wo]");
    String8 empty = {};
    CHECK_STR(Str8PushF(arena, "[%S]", empty), "[]");

    CHECK_STR(Str8PushF(arena, "%.3f", 3.14159), "3.142");
    CHECK_STR(Str8PushF(arena, "%f", 1.5), "1.500000");
    CHECK_STR(Str8PushF(arena, "%08.2f", -3.5), "-0003.50");
    CHECK_STR(Str8PushF(arena, "%e", 12345.0), "1.234500e+04");
    CHECK_STR(Str8PushF(arena, "%g", 0.5), "0.5");
    String8 big_float = Str8PushF(arena, "%.64f", 1e308);
    CHECK(big_float.size == 309 + 1 + 64);

    String8 formatted = Str8PushF(arena, "%d", 5);
    CHECK(formatted.data[formatted.size] == 0);

    // Large output: bigger than the arena's 64KB commit chunk.
    u64 big_size = 100000;
    String8 big = Str8Make(ArenaPushArray<u8>(arena, big_size), big_size);
    MemSet(big.data, 'a', big.size);
    String8 wrapped = Str8PushF(arena, "[%S]", big);
    CHECK(wrapped.size == big_size + 2);
    CHECK(wrapped.data[0] == '[' && wrapped.data[big_size + 1] == ']');
    CHECK(wrapped.data[big_size / 2] == 'a');
}

static void TestUnicode(Arena *arena)
{
    // A, e-acute (2 bytes), euro (3 bytes), grinning face (4 bytes).
    String8 text = STR8_LIT("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");

    UnicodeDecode d = Utf8Decode(text, 0);
    CHECK(d.codepoint == 'A' && d.advance == 1);
    d = Utf8Decode(text, 1);
    CHECK(d.codepoint == 0xE9 && d.advance == 2);
    d = Utf8Decode(text, 3);
    CHECK(d.codepoint == 0x20AC && d.advance == 3);
    d = Utf8Decode(text, 6);
    CHECK(d.codepoint == 0x1F600 && d.advance == 4);
    d = Utf8Decode(text, 10);
    CHECK(d.codepoint == 0 && d.advance == 0);

    String16 wide = Str16From8(arena, text);
    CHECK(wide.size == 5);
    CHECK(wide.data[0] == 0x41 && wide.data[1] == 0xE9 &&
          wide.data[2] == 0x20AC);
    CHECK(wide.data[3] == 0xD83D && wide.data[4] == 0xDE00);
    CHECK(wide.data[5] == 0);

    String8 back = Str8From16(arena, wide);
    CHECK(Str8Equal(back, text));
    CHECK(back.data[back.size] == 0);

    // The worst-case allocations are trimmed back after converting.
    u64 used_before = ArenaUsed(arena);
    Str16From8(arena, STR8_LIT("abcdefgh"));
    CHECK(ArenaUsed(arena) - used_before <= 9 * sizeof(u16) + 8);

    // Invalid input decodes to U+FFFD, one unit at a time.
    d = Utf8Decode(STR8_LIT("\xFF"), 0);
    CHECK(d.codepoint == UNICODE_REPLACEMENT && d.advance == 1);
    d = Utf8Decode(STR8_LIT("\xC0\x80"), 0); // overlong NUL
    CHECK(d.codepoint == UNICODE_REPLACEMENT && d.advance == 1);
    d = Utf8Decode(STR8_LIT("\xE2\x82"), 0); // truncated
    CHECK(d.codepoint == UNICODE_REPLACEMENT && d.advance == 1);
    d = Utf8Decode(STR8_LIT("\xED\xA0\x80"), 0); // encoded surrogate
    CHECK(d.codepoint == UNICODE_REPLACEMENT && d.advance == 1);
    d = Utf8Decode(STR8_LIT("\x80"), 0); // stray continuation
    CHECK(d.codepoint == UNICODE_REPLACEMENT && d.advance == 1);

    String16 bad16 = Str16From8(arena, STR8_LIT("a\xFFz"));
    CHECK(bad16.size == 3 && bad16.data[1] == UNICODE_REPLACEMENT);

    u16 lone_high[] = { 0xD800, 'x' };
    UnicodeDecode d16 = Utf16Decode(String16{ lone_high, 2 }, 0);
    CHECK(d16.codepoint == UNICODE_REPLACEMENT && d16.advance == 1);
    CHECK_STR(Str8From16(arena, String16{ lone_high, 2 }), "\xEF\xBF\xBDx");

    u8 encoded[4];
    CHECK(Utf8Encode(encoded, 0x110000) == 3); // out of range -> U+FFFD
    u16 encoded16[2];
    CHECK(Utf16Encode(encoded16, 0x1F600) == 2);

    String8 empty = {};
    CHECK(Str16From8(arena, empty).size == 0);
    String16 empty16 = {};
    CHECK(Str8From16(arena, empty16).size == 0);
}

static void TestFailure(void)
{
    // Every allocating function returns the empty string on the nil arena.
    String8 s = STR8_LIT("abc");
    CHECK(Str8Copy(0, s).data == 0);
    CHECK(Str8Cat(0, s, s).data == 0);
    CHECK(Str8ToUpper(0, s).data == 0);
    CHECK(Str8PushF(0, "%d", 1).data == 0);
    CHECK(Str16From8(0, s).data == 0);
    CHECK(Str8From16(0, String16{ (u16 *)L"ab", 2 }).data == 0);

    String8List list = {};
    Str8ListPush(0, &list, s);
    CHECK(list.count == 0 && list.first == 0);
    CHECK(Str8Split(0, STR8_LIT("a,b"), STR8_LIT(","), 0).count == 0);
    Str8ListPush(0, 0, s); // null list is a no-op
}

int main(void)
{
    Arena *arena = ArenaAllocDefault();

    TestMake(arena);
    TestSlice();
    TestCompare();
    TestSearch();
    TestTrim();
    TestTransform(arena);
    TestLists(arena);
    TestParse();
    TestHash();
    TestFormat(arena);
    TestUnicode(arena);
    TestFailure();

    ArenaRelease(arena);

    if (test_failures == 0)
    {
        printf("string: all tests passed\n");
    }
    return test_failures;
}
