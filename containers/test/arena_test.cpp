/*
    arena_test.cpp

    Exercises the arena: growth in place with stable pointers, zeroing,
    alignment, pop/clear/temp markers, exhaustion, and the nil arena. Prints
    FAIL lines and returns nonzero if anything failed. Build with
    `fbuild arena-test-debug` (or -release / -profile) from the repo root.

    NOTE: Uses printf until the os layer has its own output functions.
*/

#include <stdio.h>

#include "../../architecture/architecture.cpp"
#include "../../os/os_memory.cpp"
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

// Counts failed asserts instead of breaking, for tests that trigger them on
// purpose. Asserts only exist in debug, so expectations are 0 elsewhere.
static int assert_failures = 0;

static b32 TestCountAssert(const char *file,
                           int line,
                           const char *expression,
                           const char *message)
{
    (void)file;
    (void)line;
    (void)expression;
    (void)message;
    ++assert_failures;
    return 0;
}

#if defined(NDEBUG)
#define EXPECT_ASSERTS(count) CHECK(assert_failures == 0)
#else
#define EXPECT_ASSERTS(count) CHECK(assert_failures == (count))
#endif

struct TestVec
{
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};

static int arena_failures = 0;
static Arena *arena_failure_last_arena = 0;
static u64 arena_failure_last_size = 0;

static void TestArenaFailureHook(Arena *arena, u64 size, const char *reason)
{
    (void)reason;
    ++arena_failures;
    arena_failure_last_arena = arena;
    arena_failure_last_size = size;
}

static b32 TestIsZero(u8 *bytes, u64 count)
{
    for (u64 i = 0; i < count; ++i)
    {
        if (bytes[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    const u64 KB = 1024;
    const u64 MB = 1024 * KB;
    base_assert_hook = TestCountAssert;

    // Basic push: zeroed, aligned, in order.
    Arena *arena = ArenaAllocDefault();
    CHECK(!ArenaIsNil(arena));
    CHECK(ArenaUsed(arena) == 0);

    TestVec *vec = ArenaPushStruct<TestVec>(arena);
    CHECK(vec != 0);
    CHECK(TestIsZero((u8 *)vec, sizeof(TestVec)));
    vec->x = 1.0f;

    u8 *odd = (u8 *)ArenaPush(arena, 3, 1);
    u8 *aligned = (u8 *)ArenaPush(arena, 8, 64);
    CHECK(odd != 0 && aligned > odd);
    CHECK(((uptr)aligned & 63) == 0);

    // Grows across many commit chunks; early pointers never move.
    u8 *first = (u8 *)ArenaPush(arena, 100, 8);
    first[0] = 42;
    u64 committed_before = arena->committed;
    for (int i = 0; i < 32; ++i)
    {
        u8 *block = (u8 *)ArenaPush(arena, 1 * MB, 16);
        CHECK(block != 0);
        block[0] = 1;
        block[1 * MB - 1] = 2;
    }
    CHECK(arena->committed > committed_before + 32 * MB - 1);
    CHECK(arena->committed % arena->commit_chunk == 0);
    CHECK(first[0] == 42);
    CHECK(vec->x == 1.0f);

    // Alignment beyond the 64KB base alignment still holds.
    u8 *big_aligned = (u8 *)ArenaPush(arena, 16, 1 * MB);
    CHECK(((uptr)big_aligned & (1 * MB - 1)) == 0);

    // Array pushes reject element counts whose byte size overflows.
    // An overflowing count is a programmer bug: it asserts, then fails.
    CHECK(ArenaPushArray<TestVec>(arena, ~(u64)0 / 4) == 0);
    CHECK(ArenaPushArrayNoZero<TestVec>(arena, ~(u64)0 / 4) == 0);
    EXPECT_ASSERTS(2);
    assert_failures = 0;
    TestVec *vecs = ArenaPushArray<TestVec>(arena, 10);
    CHECK(!ArenaPushFailed(vecs) && ((uptr)vecs & 3) == 0);
    CHECK(TestIsZero((u8 *)vecs, 10 * sizeof(TestVec)));

    // NoZero skips zeroing but still returns usable memory.
    u8 *raw = (u8 *)ArenaPushNoZero(arena, 128, 8);
    CHECK(!ArenaPushFailed(raw));

    // Clear, then reused memory is zeroed again by a normal push.
    ArenaClear(arena);
    CHECK(ArenaUsed(arena) == 0);
    u8 *again = (u8 *)ArenaPush(arena, 1 * MB, 8);
    CHECK(TestIsZero(again, 1 * MB));
    CHECK(arena->committed > 1 * MB); // stays committed after clear

    // Temp markers rewind to the saved position.
    u64 used_before = ArenaUsed(arena);
    ArenaTemp temp = ArenaTempBegin(arena);
    ArenaPush(arena, 5000, 8);
    ArenaPush(arena, 5000, 8);
    CHECK(ArenaUsed(arena) > used_before);
    ArenaTempEnd(temp);
    CHECK(ArenaUsed(arena) == used_before);

    // Pop clamps instead of underflowing.
    ArenaPop(arena, 100);
    CHECK(ArenaUsed(arena) == used_before - 100);
    ArenaPop(arena, ~(u64)0);
    CHECK(ArenaUsed(arena) == 0);

    ArenaRelease(arena);

    // Exhaustion: a push that doesn't fit returns zeroed scratch and leaves
    // the arena unchanged.
    Arena *small = ArenaAlloc(64 * KB);
    CHECK(!ArenaIsNil(small));
    CHECK(small->reserved == 64 * KB);
    u8 *fits = (u8 *)ArenaPush(small, 32 * KB, 8);
    CHECK(fits != 0);
    u64 pos_before = small->pos;
    u8 *fail = (u8 *)ArenaPush(small, 32 * KB, 8);
    CHECK(fail == arena_nil_block);
    CHECK(ArenaPushFailed(fail));
    CHECK(!ArenaPushFailed(fits));
    CHECK(TestIsZero(fail, 32 * KB));
    CHECK(small->pos == pos_before);
    CHECK(ArenaPush(small, ARENA_NIL_BLOCK_SIZE + 1, 8) == 0);
    CHECK(ArenaPush(small, ~(u64)0, 8) == 0);
    assert_failures = 0;
    CHECK(ArenaPush(small, 16, 3) == arena_nil_block); // bad alignment
    EXPECT_ASSERTS(1);
    assert_failures = 0;
    CHECK(small->pos == pos_before);
    ArenaRelease(small);

    // Failed allocation gives the nil arena, and the nil arena is inert.
    Arena *huge = ArenaAlloc(~(u64)0);
    CHECK(ArenaIsNil(huge));
    Arena nil_copy_before = *huge;
    CHECK(ArenaPush(huge, 64, 8) == arena_nil_block);
    CHECK(ArenaPushNoZero(huge, 0, 8) == arena_nil_block);
    ArenaPop(huge, 10);
    ArenaClear(huge);
    ArenaTemp nil_temp = ArenaTempBegin(huge);
    ArenaTempEnd(nil_temp);
    ArenaRelease(huge);
    CHECK(ArenaUsed(huge) == 0);
    CHECK(huge->base == nil_copy_before.base);
    CHECK(huge->reserved == 0 && huge->committed == 0 && huge->pos == 0);

    // Null arena pointers are treated as the nil arena.
    CHECK(ArenaIsNil(0));
    CHECK(ArenaPush(0, 16, 8) == arena_nil_block);
    ArenaClear(0);
    ArenaRelease(0);

    // Popping forward means temps ended out of order: assert, then ignore.
    Arena *ordered = ArenaAlloc(1 * MB);
    assert_failures = 0;
    ArenaTemp outer = ArenaTempBegin(ordered);
    ArenaPush(ordered, 100, 1);
    ArenaTemp inner = ArenaTempBegin(ordered);
    ArenaPush(ordered, 100, 1);
    ArenaTempEnd(outer);
    CHECK(ArenaUsed(ordered) == 0);
    EXPECT_ASSERTS(0);
    ArenaTempEnd(inner); // after outer: tries to move forward
    EXPECT_ASSERTS(1);
    CHECK(ArenaUsed(ordered) == 0);
    ArenaPopTo(ordered, ordered->pos + 1000);
    EXPECT_ASSERTS(2);
    CHECK(ArenaUsed(ordered) == 0);
    ArenaRelease(ordered);
    assert_failures = 0;
    ArenaClear(0); // the nil arena may always be cleared
    ArenaTempEnd(ArenaTempBegin(0));
    CHECK(assert_failures == 0);

    // Peak tracks the high-water mark across pops.
    Arena *peaky = ArenaAlloc(1 * MB);
    CHECK(ArenaPeak(peaky) == 0);
    ArenaPush(peaky, 1000, 1);
    u64 mark = peaky->pos;
    u8 *scratch = (u8 *)ArenaPush(peaky, 256, 1);
    scratch[0] = 1;
    scratch[255] = 1;
    CHECK(ArenaPeak(peaky) == 1256);
    ArenaPopTo(peaky, mark);
    CHECK(ArenaUsed(peaky) == 1000 && ArenaPeak(peaky) == 1256);
    ArenaPush(peaky, 100, 1);
    CHECK(ArenaPeak(peaky) == 1256);
    ArenaClear(peaky);
    CHECK(ArenaPeak(peaky) == 1256);
    CHECK(ArenaPeak(0) == 0);

    // Debug builds fill popped memory so stale views stand out. Under ASan
    // the stale read below is reported as use-after-poison instead.
#if ARENA_DEBUG_FILL
    u64 fill_mark = peaky->pos;
    u8 *stale = (u8 *)ArenaPush(peaky, 64, 1);
    stale[0] = 7;
    ArenaPopTo(peaky, fill_mark);
    CHECK(stale[0] == 0xDD && stale[63] == 0xDD);
    // A normal push over the filled range is zeroed again.
    u8 *fresh = (u8 *)ArenaPush(peaky, 64, 1);
    CHECK(fresh == stale && TestIsZero(fresh, 64));
#endif
    ArenaRelease(peaky);

    // The failure hook fires for a failed alloc, then once per real arena.
    arena_failure_hook = TestArenaFailureHook;
    Arena *failed = ArenaAlloc(~(u64)0);
    CHECK(arena_failures == 1 && ArenaIsNil(arena_failure_last_arena));
    CHECK(arena_failure_last_size == ~(u64)0);
    ArenaPush(failed, 16, 8); // nil arena: already reported by ArenaAlloc
    CHECK(arena_failures == 1);

    Arena *tiny = ArenaAlloc(64 * KB);
    ArenaPush(tiny, 128 * KB, 8);
    CHECK(arena_failures == 2 && arena_failure_last_arena == tiny);
    CHECK(arena_failure_last_size == 128 * KB);
    CHECK(tiny->push_failed);
    ArenaPush(tiny, 128 * KB, 8);
    ArenaPush(tiny, 16, 3);
    CHECK(arena_failures == 2);
    // A count overflow reports like a failed push (once per arena).
    Arena *counted = ArenaAlloc(64 * KB);
    ArenaPushArray<TestVec>(counted, ~(u64)0 / 4);
    CHECK(arena_failures == 3 && arena_failure_last_arena == counted);
    ArenaRelease(counted);
    arena_failures = 2;
    ArenaRelease(tiny);

    Arena zero_arena = {};
    ArenaPush(&zero_arena, 16, 8); // zero Arena: never written, never reported
    CHECK(arena_failures == 2 && zero_arena.push_failed == 0);
    arena_failure_hook = 0;

    if (test_failures == 0)
    {
        printf("arena: all tests passed\n");
    }
    return test_failures;
}
