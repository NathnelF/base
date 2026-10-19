/*
    os_memory.h

    Platform memory: reserve address space, commit pages inside it, and give
    both back. Arenas build on this to grow in place without moving.

    Reserve claims address space only; touching it faults. Commit makes pages
    inside a reservation usable. Decommit returns the pages but keeps the
    address range. Release gives the whole reservation back.

    Semantics:
    - Reserve rounds the size up to reserve_granularity. Commit and decommit
      round the range outward to page_size.
    - Fresh commits are zeroed. Recommitting after a decommit is zeroed too.
    - Commit is idempotent on pages that are already committed.
    - OsMemRelease takes a size for the POSIX munmap case. Windows ignores it
      and releases the whole reservation. ptr must be the base that reserve
      returned.

    ZII: a null ptr or zero size is a no-op in every function. Failure is the
    zero value (null pointer or false), so callers can pass results straight
    back in without checks. OsMemGetInfo needs no init call.

    NOTE: Every name here starts with OsMem; the whole of base is one
    translation unit.

    TODO: POSIX implementation. mmap(PROT_NONE) to reserve, mprotect to
    commit, madvise(MADV_DONTNEED) plus PROT_NONE to decommit, munmap to
    release.
*/

#pragma once

#include "../architecture/arch_context.h"
#include "../types/base_types.h"

struct OsMemInfo
{
    u64 page_size;
    u64 reserve_granularity;
};

// Never null. Fills a global on first call; zero fields mean not queried yet.
OsMemInfo *OsMemGetInfo(void);

// Address space only. Null on failure.
void *OsMemReserve(u64 size);

// Like OsMemReserve but asks for a specific base address, for stable
// addresses across debug reloads. Null if the hint is unavailable.
void *OsMemReserveAt(void *hint, u64 size);

b32 OsMemCommit(void *ptr, u64 size);
void OsMemDecommit(void *ptr, u64 size);
void OsMemRelease(void *ptr, u64 size);

// Reserve and commit in one call. Null on failure.
void *OsMemReserveCommit(u64 size);
