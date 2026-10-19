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

struct TestVec
{
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};

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
    CHECK(ArenaPushArray<TestVec>(arena, ~(u64)0 / 4) == 0);
    CHECK(ArenaPushArrayNoZero<TestVec>(arena, ~(u64)0 / 4) == 0);
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
    CHECK(ArenaPush(small, 16, 3) == arena_nil_block); // bad alignment
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

    if (test_failures == 0)
    {
        printf("arena: all tests passed\n");
    }
    return test_failures;
}
