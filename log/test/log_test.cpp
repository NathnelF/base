/*
    log_test.cpp

    Exercises the log layer and the debug foundation: assert macros and the
    assert hook, log scopes (capture, nesting, dropped scopes), compile-time
    level filtering, OS error messages, and the arena failure warnings.
    Prints FAIL lines and returns nonzero if anything failed. Build with
    `fbuild log-test-debug` (or -release / -profile) from the repo root.

    Includes base.cpp rather than individual layers, so it also checks that
    the full unity build compiles in its documented order.

    NOTE: Expected counts depend on LOG_MIN_LEVEL: debug logs everything,
    release Warning and up, profile Error only.

    NOTE: Uses printf until the os layer has its own output functions.
*/

#include <stdio.h>

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

static int assert_count = 0;
static const char *assert_last_expression = 0;
static const char *assert_last_message = 0;

static b32 TestAssertHook(const char *file,
                          int line,
                          const char *expression,
                          const char *message)
{
    (void)file;
    (void)line;
    ++assert_count;
    assert_last_expression = expression;
    assert_last_message = message;
    return 0;
}

static b32 TestHas(String8 haystack, const char *needle)
{
    return Str8Find(haystack, Str8FromCStr(needle), 0) < haystack.size;
}

static u64 TestLevelsEnabled(LogLevel lowest)
{
    u64 count = 0;
    for (int level = lowest; level < LogLevel_Count; ++level)
    {
        if (level >= LOG_MIN_LEVEL)
        {
            ++count;
        }
    }
    return count;
}

static void TestAsserts(void)
{
    // The log layer installed both hooks before main.
    CHECK(base_assert_hook != 0);
    CHECK(arena_failure_hook != 0);

    AssertHookFunction *log_hook = base_assert_hook;
    base_assert_hook = TestAssertHook;

    int zero = 0;
    ASSERT(zero == 0); // passes: no call
    ASSERT_ALWAYS(zero == 0);
    CHECK(assert_count == 0);

    ASSERT(zero);
    ASSERT_MSG(zero == 1, "debug message");
    INVALID_PATH;
    NOT_IMPLEMENTED;
#if defined(NDEBUG)
    CHECK(assert_count == 0);
#else
    CHECK(assert_count == 4);
    CHECK(Str8Equal(Str8FromCStr(assert_last_expression),
                    STR8_LIT("NOT_IMPLEMENTED")));
#endif

    int before = assert_count;
    ASSERT_ALWAYS_MSG(zero == 2, "always message");
    CHECK(assert_count == before + 1);
    CHECK(
      Str8Equal(Str8FromCStr(assert_last_expression), STR8_LIT("zero == 2")));
    CHECK(
      Str8Equal(Str8FromCStr(assert_last_message), STR8_LIT("always message")));
    ASSERT_ALWAYS(zero);
    CHECK(assert_count == before + 2);

    base_assert_hook = log_hook;
}

static void TestAssertOutput(Arena *arena)
{
    // The installed hook prints through the log (asserts ignore the level
    // filter) and asks to break. Call it directly so nothing breaks.
    LogScopeBegin(arena);
    b32 should_break = base_assert_hook("game.cpp", 12, "x < y", "bad x");
    b32 also_break = base_assert_hook("game.cpp", 13, "ok", "");
    String8List lines = LogScopeEnd();
    CHECK(should_break && also_break);
    CHECK(lines.count == 2);
    if (lines.count == 2)
    {
        CHECK(Str8Equal(lines.first->string,
                        STR8_LIT("game.cpp(12): assert: x < y: bad x")));
        CHECK(
          Str8Equal(lines.last->string, STR8_LIT("game.cpp(13): assert: ok")));
    }
}

static void TestScopes(Arena *arena)
{
    LogScopeBegin(arena);
    LOG_ERROR("first %d", 1);
    LOG_ERROR("second %S", STR8_LIT("two"));
    String8List lines = LogScopeEnd();
    CHECK(lines.count == 2);
    if (lines.count == 2)
    {
        String8 first = lines.first->string;
        CHECK(TestHas(first, "log_test.cpp("));
        CHECK(Str8EndsWith(first, STR8_LIT("): error: first 1")));
        CHECK(Str8EndsWith(lines.last->string, STR8_LIT("error: second two")));
        CHECK(first.data[first.size] == 0); // scope copies are terminated
    }

    // Nested scopes: each message goes to the innermost one only.
    LogScopeBegin(arena);
    LOG_ERROR("outer a");
    LogScopeBegin(arena);
    LOG_ERROR("inner b");
    String8List inner = LogScopeEnd();
    LOG_ERROR("outer c");
    String8List outer = LogScopeEnd();
    CHECK(inner.count == 1 && outer.count == 2);
    if (inner.count == 1 && outer.count == 2)
    {
        CHECK(Str8EndsWith(inner.first->string, STR8_LIT("inner b")));
        CHECK(Str8EndsWith(outer.first->string, STR8_LIT("outer a")));
        CHECK(Str8EndsWith(outer.last->string, STR8_LIT("outer c")));
    }

    // End without begin is an empty list.
    CHECK(LogScopeEnd().count == 0);

    // A scope that cannot be allocated still pairs with its end, and the
    // enclosing scope keeps working.
    LogScopeBegin(arena);
    LogScopeBegin(0);
    LOG_ERROR("lands in the outer scope");
    String8List dropped = LogScopeEnd();
    String8List kept = LogScopeEnd();
    CHECK(dropped.count == 0);
    CHECK(kept.count == 1);

    LogScopeBegin(0);
    LogScopeBegin(arena);
    LOG_ERROR("inside");
    String8List real = LogScopeEnd();
    String8List dropped_outer = LogScopeEnd();
    CHECK(real.count == 1 && dropped_outer.count == 0);
    CHECK(LogScopeEnd().count == 0);
}

static void TestLevels(Arena *arena)
{
    LogScopeBegin(arena);
    LOG_TRACE("trace %d", 1);
    LOG_INFO("info %d", 2);
    LOG_WARN("warn %d", 3);
    LOG_ERROR("error %d", 4);
    String8List lines = LogScopeEnd();
    CHECK(lines.count == TestLevelsEnabled(LogLevel_Trace));
    CHECK(Str8EndsWith(lines.last->string, STR8_LIT("error: error 4")));
}

static void TestOsErrors(Arena *arena)
{
    String8 not_found = OsErrorMessage(arena, 2); // ERROR_FILE_NOT_FOUND
    CHECK(not_found.size > 0);
    CHECK(!CharIsSpace(not_found.data[not_found.size - 1]));
    CHECK(not_found.data[not_found.size] == 0);
    printf("ERROR_FILE_NOT_FOUND: %.*s\n",
           (int)not_found.size,
           (const char *)not_found.data);

    String8 unknown = OsErrorMessage(arena, 0xDEADBEEF);
    CHECK(Str8Equal(unknown, STR8_LIT("OS error 3735928559 (0xDEADBEEF)")));

    SetLastError(ERROR_ACCESS_DENIED);
    String8 last = OsLastErrorMessage(arena);
    CHECK(Str8Equal(last, OsErrorMessage(arena, ERROR_ACCESS_DENIED)));

    CHECK(OsErrorMessage(0, 2).size == 0);

    // Output paths: empty input, and a long line with non-ASCII text that
    // spans several conversion chunks.
    OsStdErrWrite(STR8_LIT(""));
    OsDebuggerWrite(STR8_LIT(""));
    String8List parts = {};
    for (int i = 0; i < 200; ++i)
    {
        Str8ListPush(arena, &parts, STR8_LIT("caf\xC3\xA9 \xF0\x9F\x98\x80 "));
    }
    String8 long_line = Str8Join(arena, &parts, STR8_LIT(""));
    OsDebuggerWrite(long_line);
}

static void TestOsMemWarnings(Arena *arena)
{
    // A released range is free address space, so committing into it fails.
    void *range = OsMemReserve(64 * 1024);
    OsMemRelease(range, 64 * 1024);

    LogScopeBegin(arena);
    OsMemCommit(range, 4096);
    OsMemReserve(~(u64)0);
    String8List lines = LogScopeEnd();

    u64 expected = 0;
    if constexpr (LogLevel_Warning >= LOG_MIN_LEVEL)
    {
        expected = 2;
    }
    CHECK(lines.count == expected);
    if (lines.count == 2)
    {
        String8 commit = lines.first->string;
        CHECK(TestHas(commit, "warning: OsMem commit of 4096 bytes at "));
        String8 reason = OsErrorMessage(arena, ERROR_INVALID_ADDRESS);
        CHECK(reason.size > 0 && Str8EndsWith(commit, reason));
        CHECK(TestHas(lines.last->string, "OsMem reserve (size overflows)"));
        CHECK(Str8EndsWith(lines.last->string, STR8_LIT("no OS error code")));
    }
}

static void TestArenaWarnings(Arena *arena)
{
    LogScopeBegin(arena);
    Arena *huge = ArenaAlloc(~(u64)0);
    Arena *small = ArenaAlloc(64 * 1024);
    ArenaPush(small, 128 * 1024, 8);
    ArenaPush(small, 128 * 1024, 8); // second failure: not reported again
    ArenaPush(huge, 16, 8);          // nil arena: already reported
    String8List lines = LogScopeEnd();

    u64 expected = 0;
    if constexpr (LogLevel_Warning >= LOG_MIN_LEVEL)
    {
        expected = 2;
    }
    CHECK(lines.count == expected);
    if (lines.count == 2)
    {
        CHECK(TestHas(lines.first->string, "warning: ArenaAlloc of"));
        CHECK(TestHas(lines.first->string, "reserve size overflows"));
        CHECK(TestHas(lines.last->string,
                      "push of 131072 bytes failed: out of reserved space"));
    }
    ArenaRelease(small);
}

int main(void)
{
    Arena *arena = ArenaAllocDefault();
    LogSetConsoleOutput(0);

    TestAsserts();
    TestAssertOutput(arena);
    TestScopes(arena);
    TestLevels(arena);
    TestOsErrors(arena);
    TestOsMemWarnings(arena);
    TestArenaWarnings(arena);

    // One visible line, to eyeball the console format and Unicode output.
    LogSetConsoleOutput(1);
    LOG_ERROR("sample log line: caf\xC3\xA9 \xE2\x82\xAC %d", 42);

    ArenaRelease(arena);

    if (test_failures == 0)
    {
        printf("log: all tests passed\n");
    }
    return test_failures;
}
