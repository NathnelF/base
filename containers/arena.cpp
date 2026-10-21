/*
    arena.cpp

    Implementation of arena.h. A push aligns pos, checks it against the
    reservation, commits more pages in whole commit chunks if pos passes the
    committed mark, then hands out the range. Failure paths never touch the
    arena, so the nil arena stays all zeros.

    NOTE: pos includes the header, which is why ArenaUsed
    subtracts ARENA_HEADER_SIZE.

    Poisoning: ARENA_POISON / ARENA_UNPOISON mark ranges for AddressSanitizer
    and compile to nothing otherwise. Everything committed but outside
    [header, pos) stays poisoned; a push unpoisons exactly what it hands out.
    Release unpoisons the whole block first, because the shadow memory would
    otherwise outlive the mapping and flag whatever reuses the addresses.
    ARENA_DEBUG_FILL is the non-ASan debug equivalent for pops.

    TODO: Track a high-water mark in the header. Memory past it has never
    been handed out and is still zero from the OS commit, so ArenaPush could
    skip zeroing it. That avoids touching every page of a large, sparsely
    used push.
*/

#include "arena.h"
#include "../os/os_memory.h"
#include "../types/base_memory.h"

#if defined(__SANITIZE_ADDRESS__)
#define ARENA_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ARENA_ASAN 1
#endif
#endif
#ifndef ARENA_ASAN
#define ARENA_ASAN 0
#endif

#if ARENA_ASAN
#include <sanitizer/asan_interface.h>
#define ARENA_POISON(ptr, size) ASAN_POISON_MEMORY_REGION((ptr), (size))
#define ARENA_UNPOISON(ptr, size) ASAN_UNPOISON_MEMORY_REGION((ptr), (size))
#else
#define ARENA_POISON(ptr, size) ((void)(ptr), (void)(size))
#define ARENA_UNPOISON(ptr, size) ((void)(ptr), (void)(size))
#endif

#if !defined(NDEBUG) && !ARENA_ASAN
#define ARENA_DEBUG_FILL 1
#else
#define ARENA_DEBUG_FILL 0
#endif

constexpr u8 ARENA_POP_FILL = 0xDD;

static Arena arena_nil;
static u8 arena_nil_block[ARENA_NIL_BLOCK_SIZE];

// Rounds value up to a power-of-two alignment. Returns 0 on overflow.
static u64 ArenaAlignUp(u64 value, u64 alignment)
{
    u64 mask = alignment - 1;
    if (value > ~(u64)0 - mask)
    {
        return 0;
    }
    return (value + mask) & ~mask;
}

static b32 ArenaIsPowerOfTwo(u64 value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static Arena *ArenaOrNil(Arena *arena)
{
    if (arena == 0)
    {
        return &arena_nil;
    }
    return arena;
}

// What a failed push returns: zeroed shared scratch if it fits, else null.
static void *ArenaFailedPush(u64 size)
{
    if (size > ARENA_NIL_BLOCK_SIZE)
    {
        return 0;
    }
    MemZero(arena_nil_block, size);
    return arena_nil_block;
}

static void ArenaReportFailure(Arena *arena, u64 size, const char *reason)
{
    if (arena_failure_hook != 0)
    {
        arena_failure_hook(arena, size, reason);
    }
}

// Reports the first failed push of a real arena. Never writes the nil arena
// or a zero Arena, so their failures (already reported by ArenaAlloc, or a
// deliberately empty arena) stay quiet.
static void ArenaReportPushFailure(Arena *arena, u64 size, const char *reason)
{
    if (arena->reserved != 0 && !arena->push_failed)
    {
        arena->push_failed = 1;
        ArenaReportFailure(arena, size, reason);
    }
}

// Returns null on failure and leaves the arena untouched in that case;
// *reason then says why.
static void *ArenaPushRaw(Arena *arena,
                          u64 size,
                          u64 align,
                          const char **reason)
{
    *reason = "out of reserved space";
    if (arena->reserved == 0)
    {
        *reason = "nil arena";
        return 0;
    }
    if (align == 0)
    {
        align = 1;
    }
    ASSERT_MSG(ArenaIsPowerOfTwo(align),
               "ArenaPush: alignment is not a power of two");
    if (!ArenaIsPowerOfTwo(align))
    {
        *reason = "alignment is not a power of two";
        return 0;
    }

    // Align the address, not the offset, so alignments above the base's own
    // alignment still hold.
    uptr base_address = (uptr)arena->base;
    u64 aligned_address = ArenaAlignUp(base_address + arena->pos, align);
    if (aligned_address == 0)
    {
        return 0;
    }
    u64 start = aligned_address - base_address;
    if (start > arena->reserved)
    {
        return 0;
    }
    if (size > arena->reserved - start)
    {
        return 0;
    }
    u64 end = start + size;

    if (end > arena->committed)
    {
        u64 new_committed = ArenaAlignUp(end, arena->commit_chunk);
        if (new_committed == 0 || new_committed > arena->reserved)
        {
            new_committed = arena->reserved;
        }
        u64 grow = new_committed - arena->committed;
        if (!OsMemCommit(arena->base + arena->committed, grow))
        {
            *reason = "OS commit failed";
            return 0;
        }
        ARENA_POISON(arena->base + arena->committed, grow);
        arena->committed = new_committed;
    }

    ARENA_UNPOISON(arena->base + start, size);
    arena->pos = end;
    if (end > arena->peak_pos)
    {
        arena->peak_pos = end;
    }
    return arena->base + start;
}

Arena *ArenaAlloc(u64 reserve_size)
{
    OsMemInfo *info = OsMemGetInfo();
    u64 reserved = reserve_size;
    if (reserved < ARENA_HEADER_SIZE)
    {
        reserved = ARENA_HEADER_SIZE;
    }
    reserved = ArenaAlignUp(reserved, info->reserve_granularity);
    if (reserved == 0)
    {
        ArenaReportFailure(&arena_nil, reserve_size, "reserve size overflows");
        return &arena_nil;
    }

    u64 commit_chunk = ARENA_COMMIT_CHUNK;
    if (commit_chunk < info->page_size)
    {
        commit_chunk = info->page_size;
    }
    u64 committed = ArenaAlignUp(ARENA_HEADER_SIZE, commit_chunk);
    if (committed > reserved)
    {
        committed = reserved;
    }

    u8 *base = (u8 *)OsMemReserve(reserved);
    if (base == 0)
    {
        ArenaReportFailure(&arena_nil, reserve_size, "OS reserve failed");
        return &arena_nil;
    }
    if (!OsMemCommit(base, committed))
    {
        OsMemRelease(base, reserved);
        ArenaReportFailure(&arena_nil, reserve_size, "OS commit failed");
        return &arena_nil;
    }

    Arena *arena = (Arena *)base;
    arena->base = base;
    arena->reserved = reserved;
    arena->committed = committed;
    arena->pos = ARENA_HEADER_SIZE;
    arena->commit_chunk = commit_chunk;
    arena->peak_pos = ARENA_HEADER_SIZE;
    arena->push_failed = 0;
    ARENA_POISON(base + ARENA_HEADER_SIZE, committed - ARENA_HEADER_SIZE);
    return arena;
}

Arena *ArenaAllocDefault(void)
{
    return ArenaAlloc(ARENA_DEFAULT_RESERVE);
}

void ArenaRelease(Arena *arena)
{
    arena = ArenaOrNil(arena);
    // The header is inside the block, so read both fields before releasing.
    u8 *base = arena->base;
    u64 reserved = arena->reserved;
    ARENA_UNPOISON(base, arena->committed);
    OsMemRelease(base, reserved);
}

b32 ArenaIsNil(Arena *arena)
{
    return arena == 0 || arena->reserved == 0;
}

void *ArenaPush(Arena *arena, u64 size, u64 align)
{
    arena = ArenaOrNil(arena);
    const char *reason = 0;
    void *result = ArenaPushRaw(arena, size, align, &reason);
    if (result == 0)
    {
        ArenaReportPushFailure(arena, size, reason);
        return ArenaFailedPush(size);
    }
    MemZero(result, size);
    return result;
}

void *ArenaPushNoZero(Arena *arena, u64 size, u64 align)
{
    arena = ArenaOrNil(arena);
    const char *reason = 0;
    void *result = ArenaPushRaw(arena, size, align, &reason);
    if (result == 0)
    {
        ArenaReportPushFailure(arena, size, reason);
        return ArenaFailedPush(size);
    }
    return result;
}

b32 ArenaPushFailed(void *ptr)
{
    return ptr == 0 || ptr == arena_nil_block;
}

void ArenaReportCountOverflow(Arena *arena, u64 count, u64 element_size)
{
    (void)count;
    (void)element_size;
    ASSERT_MSG(element_size == 0 || count <= ~(u64)0 / element_size,
               "ArenaPushArray: count * sizeof(T) overflows u64");
    ArenaReportPushFailure(
      ArenaOrNil(arena), ~(u64)0, "element count * size overflows u64");
}

u64 ArenaUsed(Arena *arena)
{
    arena = ArenaOrNil(arena);
    if (arena->pos < ARENA_HEADER_SIZE)
    {
        return 0;
    }
    return arena->pos - ARENA_HEADER_SIZE;
}

u64 ArenaPeak(Arena *arena)
{
    arena = ArenaOrNil(arena);
    if (arena->peak_pos < ARENA_HEADER_SIZE)
    {
        return 0;
    }
    return arena->peak_pos - ARENA_HEADER_SIZE;
}

void ArenaPopTo(Arena *arena, u64 pos)
{
    arena = ArenaOrNil(arena);
    if (pos < ARENA_HEADER_SIZE)
    {
        pos = ARENA_HEADER_SIZE;
    }
    // Moving forward means temps ended out of order, or a temp outlived a
    // clear. The nil arena (reserved 0, pos 0) is exempt: clearing it is fine.
    ASSERT_MSG(arena->reserved == 0 || pos <= arena->pos,
               "ArenaPopTo: position is past the current one (temps ended out "
               "of order?)");
    // Only moves backwards, so the nil arena (pos 0) is never written.
    if (pos < arena->pos)
    {
        u64 popped = arena->pos - pos;
#if ARENA_DEBUG_FILL
        MemSet(arena->base + pos, ARENA_POP_FILL, popped);
#endif
        ARENA_POISON(arena->base + pos, popped);
        arena->pos = pos;
    }
}

void ArenaPop(Arena *arena, u64 size)
{
    arena = ArenaOrNil(arena);
    u64 used = ArenaUsed(arena);
    if (size > used)
    {
        size = used;
    }
    ArenaPopTo(arena, arena->pos - size);
}

void ArenaClear(Arena *arena)
{
    ArenaPopTo(arena, ARENA_HEADER_SIZE);
}

ArenaTemp ArenaTempBegin(Arena *arena)
{
    arena = ArenaOrNil(arena);
    ArenaTemp temp;
    temp.arena = arena;
    temp.pos = arena->pos;
    return temp;
}

void ArenaTempEnd(ArenaTemp temp)
{
    ArenaPopTo(temp.arena, temp.pos);
}
