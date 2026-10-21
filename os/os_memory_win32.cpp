/*
    os_memory_win32.cpp

    Windows backend for os_memory.h. Included by os_memory.cpp, not built on
    its own. Reserve is VirtualAlloc(MEM_RESERVE), commit is
    VirtualAlloc(MEM_COMMIT), decommit and release are VirtualFree.

    NOTE: Includes windows.h, which lands in the consumer's translation unit.
    WIN32_LEAN_AND_MEAN and NOMINMAX limit the macro pollution. Hand-declaring
    the few kernel32 functions would clash with a game that includes
    windows.h itself.
*/

#if OS_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static void OsMemWin32QueryInfo(OsMemInfo *info)
{
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    info->page_size = system_info.dwPageSize;
    info->reserve_granularity = system_info.dwAllocationGranularity;
}

static void *OsMemWin32Reserve(void *hint, u64 size)
{
    return VirtualAlloc(hint, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
}

static b32 OsMemWin32Commit(void *ptr, u64 size)
{
    void *result = VirtualAlloc(ptr, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE);
    return result != 0;
}

static b32 OsMemWin32Decommit(void *ptr, u64 size)
{
    return VirtualFree(ptr, (SIZE_T)size, MEM_DECOMMIT) != 0;
}

static b32 OsMemWin32Release(void *ptr, u64 size)
{
    (void)size;
    return VirtualFree(ptr, 0, MEM_RELEASE) != 0;
}

static u32 OsMemWin32LastError(void)
{
    return GetLastError();
}

#endif
