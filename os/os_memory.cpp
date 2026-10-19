/*
    os_memory.cpp

    Platform-independent half of os_memory.h: size rounding, the cached
    OsMemInfo, and the ZII guards. The OS calls themselves come from the
    backend included below, which provides OsMem<Os>QueryInfo, Reserve,
    Commit, Decommit and Release.

    NOTE: OsMemGetInfo fills its global without a lock. Racing first calls
    write identical values, which is benign.
*/

#include "os_memory.h"

#include "os_memory_win32.cpp"

#if OS_WINDOWS
#define OsMemBackendQueryInfo OsMemWin32QueryInfo
#define OsMemBackendReserve OsMemWin32Reserve
#define OsMemBackendCommit OsMemWin32Commit
#define OsMemBackendDecommit OsMemWin32Decommit
#define OsMemBackendRelease OsMemWin32Release
#else
#error "os_memory: no backend for this OS yet"
#endif

static OsMemInfo os_mem_info;

// Rounds value up to a power-of-two alignment. Returns 0 on overflow.
static u64 OsMemRoundUp(u64 value, u64 alignment)
{
    u64 mask = alignment - 1;
    if (value > ~(u64)0 - mask)
    {
        return 0;
    }
    return (value + mask) & ~mask;
}

OsMemInfo *OsMemGetInfo(void)
{
    if (os_mem_info.page_size == 0)
    {
        OsMemBackendQueryInfo(&os_mem_info);
    }
    return &os_mem_info;
}

void *OsMemReserveAt(void *hint, u64 size)
{
    if (size == 0)
    {
        return 0;
    }
    OsMemInfo *info = OsMemGetInfo();
    u64 rounded = OsMemRoundUp(size, info->reserve_granularity);
    if (rounded == 0)
    {
        return 0;
    }
    return OsMemBackendReserve(hint, rounded);
}

void *OsMemReserve(u64 size)
{
    return OsMemReserveAt(0, size);
}

// Rounds [ptr, ptr + size) outward to page boundaries. Returns false if the
// inputs are empty or the range overflows.
static b32 OsMemPageRange(void **ptr, u64 *size)
{
    if (*ptr == 0 || *size == 0)
    {
        return 0;
    }
    u64 page_size = OsMemGetInfo()->page_size;
    uptr start = (uptr)*ptr & ~(uptr)(page_size - 1);
    u64 length = *size + ((uptr)*ptr - start);
    if (length < *size)
    {
        return 0;
    }
    length = OsMemRoundUp(length, page_size);
    if (length == 0)
    {
        return 0;
    }
    *ptr = (void *)start;
    *size = length;
    return 1;
}

b32 OsMemCommit(void *ptr, u64 size)
{
    if (!OsMemPageRange(&ptr, &size))
    {
        return 0;
    }
    return OsMemBackendCommit(ptr, size);
}

void OsMemDecommit(void *ptr, u64 size)
{
    if (OsMemPageRange(&ptr, &size))
    {
        OsMemBackendDecommit(ptr, size);
    }
}

void OsMemRelease(void *ptr, u64 size)
{
    if (ptr != 0)
    {
        OsMemBackendRelease(ptr, size);
    }
}

void *OsMemReserveCommit(u64 size)
{
    void *ptr = OsMemReserve(size);
    if (ptr == 0)
    {
        return 0;
    }
    if (!OsMemCommit(ptr, size))
    {
        OsMemRelease(ptr, size);
        return 0;
    }
    return ptr;
}
