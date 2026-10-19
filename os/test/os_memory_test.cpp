/*
    os_memory_test.cpp

    Exercises the os memory API: reserve, grow-in-place commit, decommit,
    reserve at a hint, and the ZII no-op cases. Prints FAIL lines and returns
    nonzero if anything failed. Build with `fbuild os-memory-test-debug` (or
    -release / -profile) from the repo root.

    NOTE: Uses printf until the os layer has its own output functions.
*/

#include <stdio.h>

#include "../../architecture/architecture.cpp"
#include "../os.cpp"

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

static b32 TestIsPowerOfTwo(u64 value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

int main(void)
{
    const u64 KB = 1024;
    const u64 MB = 1024 * KB;
    const u64 GB = 1024 * MB;

    OsMemInfo *info = OsMemGetInfo();
    CHECK(info != 0);
    CHECK(TestIsPowerOfTwo(info->page_size));
    CHECK(TestIsPowerOfTwo(info->reserve_granularity));
    CHECK(info->reserve_granularity >= info->page_size);
    printf("page %llu, granularity %llu\n",
           (unsigned long long)info->page_size,
           (unsigned long long)info->reserve_granularity);

    // Grow in place inside one reservation.
    u8 *base = (u8 *)OsMemReserve(1 * GB);
    CHECK(base != 0);
    CHECK(OsMemCommit(base, 1 * MB));
    CHECK(TestIsZero(base, 1 * MB));
    for (u64 i = 0; i < 1 * MB; ++i)
    {
        base[i] = (u8)(i * 7 + 1);
    }
    CHECK(OsMemCommit(base + 1 * MB, 1 * MB));
    CHECK(TestIsZero(base + 1 * MB, 1 * MB));
    b32 intact = 1;
    for (u64 i = 0; i < 1 * MB; ++i)
    {
        if (base[i] != (u8)(i * 7 + 1))
        {
            intact = 0;
            break;
        }
    }
    CHECK(intact);

    // Recommitting committed pages keeps their contents.
    CHECK(OsMemCommit(base, 1 * MB));
    CHECK(base[3] == (u8)(3 * 7 + 1));

    // Decommit then recommit comes back zeroed.
    OsMemDecommit(base, 1 * MB);
    CHECK(OsMemCommit(base, 1 * MB));
    CHECK(TestIsZero(base, 1 * MB));

    // Unaligned commit rounds outward to pages.
    CHECK(OsMemCommit(base + 2 * MB + 5, 10));
    base[2 * MB + 5] = 1;
    OsMemRelease(base, 1 * GB);

    // Reserve at a hint: first gets it, a second claim on the same spot fails.
    void *first = OsMemReserve(1 * MB);
    CHECK(first != 0);
    OsMemRelease(first, 1 * MB);
    void *hinted = OsMemReserveAt(first, 1 * MB);
    CHECK(hinted == first);
    void *taken = OsMemReserveAt(first, 1 * MB);
    CHECK(taken == 0);
    OsMemRelease(hinted, 1 * MB);

    // ZII: nulls, zeros and absurd sizes do nothing and do not crash.
    CHECK(OsMemReserve(0) == 0);
    CHECK(OsMemReserve(~(u64)0) == 0);
    CHECK(OsMemReserveCommit(0) == 0);
    CHECK(!OsMemCommit(0, 4 * KB));
    CHECK(!OsMemCommit(base, 0));
    OsMemDecommit(0, 4 * KB);
    OsMemDecommit(base, 0);
    OsMemRelease(0, 0);

    // One-call reserve + commit.
    u8 *both = (u8 *)OsMemReserveCommit(64 * KB);
    CHECK(both != 0);
    if (both != 0)
    {
        CHECK(TestIsZero(both, 64 * KB));
        both[0] = 1;
        both[64 * KB - 1] = 2;
    }
    OsMemRelease(both, 64 * KB);

    if (test_failures == 0)
    {
        printf("os memory: all tests passed\n");
    }
    return test_failures;
}
