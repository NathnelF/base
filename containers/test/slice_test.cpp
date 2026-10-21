/*
    slice_test.cpp

    Exercises Slice<T>: arena pushes, indexing, sub-slicing and clamping,
    copies, range-for, empty and failed slices, and the out-of-bounds nil
    element. Prints FAIL lines and returns nonzero if anything failed. Build
    with `fbuild slice-test-debug` (or -release / -profile) from the repo
    root.

    NOTE: The out-of-bounds case only runs with NDEBUG (release/profile);
    in debug the ASSERT would break into the debugger.

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

int main(void)
{
    base_assert_hook = TestCountAssert;
    Arena *arena = ArenaAllocDefault();

    // Push: zeroed, aligned, indexable.
    Slice<TestVec> vecs = SlicePush<TestVec>(arena, 10);
    CHECK(vecs.count == 10);
    CHECK(vecs.data != 0 && ((uptr)vecs.data & (alignof(TestVec) - 1)) == 0);
    b32 zeroed = 1;
    for (TestVec &vec : vecs)
    {
        if (vec.x != 0 || vec.y != 0 || vec.z != 0 || vec.w != 0)
        {
            zeroed = 0;
        }
    }
    CHECK(zeroed);

    for (u64 i = 0; i < vecs.count; ++i)
    {
        vecs[i].x = (f32)i;
    }
    CHECK(vecs[3].x == 3.0f);
    f32 sum = 0;
    for (TestVec &vec : vecs)
    {
        sum += vec.x;
    }
    CHECK(sum == 45.0f);

    // Sub-slices share memory and clamp.
    Slice<int> ints = SlicePush<int>(arena, 8);
    for (u64 i = 0; i < ints.count; ++i)
    {
        ints[i] = (int)i;
    }
    Slice<int> middle = SliceSub(ints, 2, 5);
    CHECK(middle.count == 3 && middle[0] == 2 && middle[2] == 4);
    middle[0] = 100;
    CHECK(ints[2] == 100);
    CHECK(SliceSub(ints, 6, 100).count == 2);
    CHECK(SliceSub(ints, 5, 2).count == 0);
    CHECK(SliceSub(ints, 100, 200).count == 0);
    CHECK(SliceSub(ints, 100, 200).data == 0);
    CHECK(SlicePrefix(ints, 3).count == 3 && SlicePrefix(ints, 3)[2] == 100);
    CHECK(SlicePrefix(ints, 50).count == 8);
    CHECK(SliceSkip(ints, 6).count == 2 && SliceSkip(ints, 6)[0] == 6);
    CHECK(SliceSkip(ints, 50).count == 0);

    // Copies are independent.
    Slice<int> copy = SliceCopy(arena, ints);
    CHECK(copy.count == ints.count && copy.data != ints.data);
    CHECK(copy[2] == 100 && copy[7] == 7);
    copy[0] = -1;
    CHECK(ints[0] == 0);
    CHECK(SliceCopy(arena, Slice<int>{}).count == 0);

    // Views over non-arena memory.
    int stack_ints[4] = { 1, 2, 3, 4 };
    Slice<int> view = SliceMake(stack_ints, 4);
    CHECK(view.count == 4 && view[3] == 4);

    // Empty slices: zero value, null data, zero count.
    Slice<int> zero = {};
    CHECK(zero.count == 0 && zero.data == 0);
    CHECK(SliceMake((int *)0, 5).count == 0);
    CHECK(SlicePush<int>(arena, 0).count == 0);
    int iterations = 0;
    for (int value : zero)
    {
        iterations += value + 1;
    }
    CHECK(iterations == 0);

    // Failed pushes give empty slices, never the arena's nil scratch.
    Slice<int> from_nil = SlicePush<int>(0, 16);
    CHECK(from_nil.count == 0 && from_nil.data == 0);
    Slice<TestVec> overflow = SlicePush<TestVec>(arena, ~(u64)0 / 4);
    CHECK(overflow.count == 0 && overflow.data == 0);
    EXPECT_ASSERTS(1); // an overflowing count is a programmer bug
    assert_failures = 0;
    Arena *small = ArenaAlloc(64 * 1024);
    Slice<u8> too_big = SlicePushNoZero<u8>(small, 128 * 1024);
    CHECK(too_big.count == 0 && too_big.data == 0);
    ArenaRelease(small);

#if defined(NDEBUG)
    // Out of bounds: zeroed nil element, re-zeroed after a write.
    TestVec &oob = vecs[vecs.count];
    CHECK(oob.x == 0);
    oob.x = 5.0f;
    CHECK(vecs[1000].x == 0);
    CHECK(zero[0] == 0);
#endif

    ArenaRelease(arena);

    if (test_failures == 0)
    {
        printf("slice: all tests passed\n");
    }
    return test_failures;
}
