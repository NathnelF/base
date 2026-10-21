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
#include "../os_memory.cpp"

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

static int os_mem_failures = 0;
static const char *os_mem_last_operation = 0;
static u32 os_mem_last_error = 0;

static void TestOsMemFailure(const char *operation,
                             void *ptr,
                             u64 size,
                             u32 os_error)
{
    (void)ptr;
    (void)size;
    ++os_mem_failures;
    os_mem_last_operation = operation;
    os_mem_last_error = os_error;
}

static b32 TestLastFailure(const char *operation, b32 expect_os_error)
{
    if (os_mem_last_operation == 0)
    {
        return 0;
    }
    const char *a = os_mem_last_operation;
    const char *b = operation;
    while (*a != 0 && *a == *b)
    {
        ++a;
        ++b;
    }
    b32 same = *a == *b;
    return same && ((os_mem_last_error != 0) == (expect_os_error != 0));
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

static b32 TestIsPowerOfTwo(u64 value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

int main(void)
{
    const u64 KB = 1024;
    const u64 MB = 1024 * KB;
    const u64 GB = 1024 * MB;
    base_assert_hook = TestCountAssert;
    os_mem_failure_hook = TestOsMemFailure;

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

    // Unaligned decommit rounds inward: the partly covered pages at both ends
    // keep their data, and only the whole page between them is freed.
    u64 page = info->page_size;
    u8 *pages = base + 4 * MB;
    CHECK(OsMemCommit(pages, 3 * page));
    pages[page - 1] = 11;
    pages[page] = 22;
    pages[2 * page] = 33;
    OsMemDecommit(pages + page - 1, page + 2);
    CHECK(pages[page - 1] == 11);
    CHECK(pages[2 * page] == 33);
    CHECK(OsMemCommit(pages + page, page));
    CHECK(pages[page] == 0);

    // A decommit inside a single page frees nothing.
    pages[page + 10] = 44;
    OsMemDecommit(pages + page + 8, 100);
    CHECK(pages[page + 10] == 44);
    OsMemRelease(base, 1 * GB);

    // Reserve at a hint: first gets it, a second claim on the same spot fails.
    void *first = OsMemReserve(1 * MB);
    CHECK(first != 0);
    OsMemRelease(first, 1 * MB);
    void *hinted = OsMemReserveAt(first, 1 * MB);
    CHECK(hinted == first);
    CHECK(os_mem_failures == 0); // nothing above failed
    void *taken = OsMemReserveAt(first, 1 * MB);
    CHECK(taken == 0);
    CHECK(os_mem_failures == 1 && TestLastFailure("reserve", 1));
    OsMemRelease(hinted, 1 * MB);

    // An unaligned hint is a programmer bug: it asserts, then is refused
    // rather than rounded to another address. No OS call, so no report.
    CHECK(OsMemReserveAt((u8 *)first + info->page_size, 1 * MB) == 0);
    EXPECT_ASSERTS(1);
    CHECK(os_mem_failures == 1);

    // Failed OS calls are reported with the OS error code. first is released,
    // so its range is free address space now.
    CHECK(!OsMemCommit(first, 4 * KB));
    CHECK(os_mem_failures == 2 && TestLastFailure("commit", 1));
    OsMemDecommit(first, 64 * KB);
    CHECK(os_mem_failures == 3 && TestLastFailure("decommit", 1));
    int not_a_reservation = 0;
    OsMemRelease(&not_a_reservation, 4 * KB);
    CHECK(os_mem_failures == 4 && TestLastFailure("release", 1));

    // ZII: nulls, zeros and absurd sizes do nothing and do not crash.
    int failures_before_zii = os_mem_failures;
    CHECK(OsMemReserve(0) == 0);
    CHECK(os_mem_failures == failures_before_zii); // zero size: a no-op
    CHECK(OsMemReserve(~(u64)0) == 0);
    CHECK(os_mem_failures == failures_before_zii + 1);
    CHECK(TestLastFailure("reserve (size overflows)", 0));
    failures_before_zii = os_mem_failures;
    CHECK(OsMemReserveCommit(0) == 0);
    CHECK(!OsMemCommit(0, 4 * KB));
    CHECK(!OsMemCommit(base, 0));
    OsMemDecommit(0, 4 * KB);
    OsMemDecommit(base, 0);
    OsMemRelease(0, 0);
    CHECK(os_mem_failures == failures_before_zii);
    EXPECT_ASSERTS(1);

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
