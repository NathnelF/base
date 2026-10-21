/*
    arena.h

    Linear (bump) allocator over a reserved block of address space. The arena
    reserves its whole capacity up front and commits pages as pos advances, so
    pointers into it never move and unused capacity costs no memory.

    The Arena header lives at the start of the reservation, so ArenaAlloc
    returns a pointer and ArenaRelease frees everything with one call.

    Pushing returns zeroed memory unless you use the NoZero variant. Pop,
    clear and temp markers only move pos back; committed pages stay committed
    so refilling is cheap. TODO: ArenaTrim to decommit unused pages.

    NOTE: ArenaPop(size) removes bytes, not pushes. Alignment padding before
    a push is not counted in its size, so popping that size can leave the
    padding behind. Prefer temp markers or ArenaPopTo for exact undo.

    ZII: the zero Arena is a valid arena with no capacity, and a null Arena *
    is treated as one. ArenaAlloc returns the global nil arena on failure.
    Every function accepts the nil arena without checks and never writes to
    it. A push that cannot be satisfied returns zeroed scratch memory (never
    null) when size <= ARENA_NIL_BLOCK_SIZE. The scratch is shared, so treat
    it as read-mostly: the program keeps running with zeros, not crashing.
    Larger failed pushes return null. ArenaIsNil detects a failed
    ArenaAlloc, and ArenaPushFailed detects a failed push.

    Typed pushes are templates: ArenaPushStruct<T>, ArenaPushArray<T> and
    ArenaPushArrayNoZero<T> fill in size and alignment. ArenaPushArray<T>
    returns null if sizeof(T) * count overflows u64.

    NOTE: The scratch block is one global with no synchronization. Threads
    whose pushes fail at the same time share it, and their writes race. That
    is acceptable for a last-resort fallback, but never rely on its contents.

    Alignment is a power of two and 0 means none. It applies to the returned
    address, so any alignment works.

    Programmer bugs ASSERT, then fail the ZII way so release keeps running:
    - an alignment that is not a power of two (the push fails),
    - ArenaPushArray<T> with a count whose byte size overflows u64 (returns
      null, and is reported through the failure hook like a failed push),
    - ArenaPopTo / ArenaTempEnd to a position past the current one, which
      means temps were ended out of order or the arena was cleared in
      between (ignored).

    Failures are not silent: arena_failure_hook (installed by the log layer)
    reports a failed ArenaAlloc, and the first failed push of each arena.

    Debugging: in debug builds popped memory is filled with 0xDD, so reads
    through a slice or string that outlived its pop show up as 0xDDDD...
    instead of plausible old data. Under AddressSanitizer the arena poisons
    everything outside [header, pos) instead, so those reads are reported
    at the faulting line. ArenaPeak gives the high-water mark for sizing.

    NOTE: Every name here starts with Arena or ARENA; the whole of base is one
    translation unit.

    TODO: Per-thread scratch arenas once the threading layer exists.
*/

#pragma once

#include "../types/base_assert.h"
#include "../types/base_types.h"

constexpr u64 ARENA_HEADER_SIZE = 64;
constexpr u64 ARENA_DEFAULT_RESERVE = 64ull * 1024 * 1024 * 1024;
constexpr u64 ARENA_COMMIT_CHUNK = 64 * 1024;
constexpr u64 ARENA_NIL_BLOCK_SIZE = 64 * 1024;

struct Arena
{
    u8 *base; // start of the reservation; also where this header lives
    u64 reserved;
    u64 committed;
    u64 pos; // offset from base; never below ARENA_HEADER_SIZE once allocated
    u64 commit_chunk;
    u64 peak_pos;    // highest pos ever reached
    b32 push_failed; // set on the first failed push, so it is reported once
};

static_assert(sizeof(Arena) <= ARENA_HEADER_SIZE, "Arena header too big");

// Called when ArenaAlloc fails (arena is the nil arena, size the requested
// reserve) and on the first failed push of each real arena. The log layer
// installs one that logs a warning; base_assert_hook explains the pattern.
typedef void ArenaFailureHookFunction(Arena *arena,
                                      u64 size,
                                      const char *reason);

inline ArenaFailureHookFunction *arena_failure_hook = 0;

// A saved position, for scoped allocations.
struct ArenaTemp
{
    Arena *arena;
    u64 pos;
};

// Reserves reserve_size (rounded up to the OS granularity) and commits the
// first chunk. Returns the nil arena on failure.
Arena *ArenaAlloc(u64 reserve_size);
Arena *ArenaAllocDefault(void);
void ArenaRelease(Arena *arena);

b32 ArenaIsNil(Arena *arena);

void *ArenaPush(Arena *arena, u64 size, u64 align);
void *ArenaPushNoZero(Arena *arena, u64 size, u64 align);

// True for null or the shared nil scratch, i.e. any push that failed.
b32 ArenaPushFailed(void *ptr);

// Reports an ArenaPushArray<T> count overflow; used by the templates.
void ArenaReportCountOverflow(Arena *arena, u64 count, u64 element_size);

// Bytes in use, header excluded.
u64 ArenaUsed(Arena *arena);
// Most bytes ever in use at once, header excluded.
u64 ArenaPeak(Arena *arena);

void ArenaPop(Arena *arena, u64 size);
void ArenaPopTo(Arena *arena, u64 pos);
void ArenaClear(Arena *arena);

ArenaTemp ArenaTempBegin(Arena *arena);
void ArenaTempEnd(ArenaTemp temp);

template<typename T>
T *ArenaPushStruct(Arena *arena)
{
    return (T *)ArenaPush(arena, sizeof(T), alignof(T));
}

template<typename T>
T *ArenaPushArray(Arena *arena, u64 count)
{
    if (count > ~(u64)0 / sizeof(T))
    {
        ArenaReportCountOverflow(arena, count, sizeof(T));
        return 0;
    }
    return (T *)ArenaPush(arena, sizeof(T) * count, alignof(T));
}

template<typename T>
T *ArenaPushArrayNoZero(Arena *arena, u64 count)
{
    if (count > ~(u64)0 / sizeof(T))
    {
        ArenaReportCountOverflow(arena, count, sizeof(T));
        return 0;
    }
    return (T *)ArenaPushNoZero(arena, sizeof(T) * count, alignof(T));
}
