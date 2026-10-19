/*
    arena.cpp

    Implementation of arena.h. A push aligns pos, checks it against the
    reservation, commits more pages in whole commit chunks if pos passes the
    committed mark, then hands out the range. Failure paths never touch the
    arena, so the nil arena stays all zeros.

    NOTE: pos includes the header, which is why ArenaUsed
    subtracts ARENA_HEADER_SIZE.

    TODO: Track a high-water mark in the header. Memory past it has never
    been handed out and is still zero from the OS commit, so ArenaPush could
    skip zeroing it. That avoids touching every page of a large, sparsely
    used push.
*/

#include "arena.h"
#include "../os/os_memory.h"

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

// TODO: Wide/SIMD stores for large clears.
static void ArenaZeroBytes(void *ptr, u64 size)
{
    u8 *bytes = (u8 *)ptr;
    while (size > 0 && ((uptr)bytes & 7) != 0)
    {
        *bytes = 0;
        ++bytes;
        --size;
    }
    while (size >= 8)
    {
        *(u64 *)bytes = 0;
        bytes += 8;
        size -= 8;
    }
    while (size > 0)
    {
        *bytes = 0;
        ++bytes;
        --size;
    }
}

// What a failed push returns: zeroed shared scratch if it fits, else null.
static void *ArenaFailedPush(u64 size)
{
    if (size > ARENA_NIL_BLOCK_SIZE)
    {
        return 0;
    }
    ArenaZeroBytes(arena_nil_block, size);
    return arena_nil_block;
}

// Returns null on failure and leaves the arena untouched in that case.
static void *ArenaPushRaw(Arena *arena, u64 size, u64 align)
{
    if (arena->reserved == 0)
    {
        return 0;
    }
    if (align == 0)
    {
        align = 1;
    }
    if (!ArenaIsPowerOfTwo(align))
    {
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
            return 0;
        }
        arena->committed = new_committed;
    }

    arena->pos = end;
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
        return &arena_nil;
    }
    if (!OsMemCommit(base, committed))
    {
        OsMemRelease(base, reserved);
        return &arena_nil;
    }

    Arena *arena = (Arena *)base;
    arena->base = base;
    arena->reserved = reserved;
    arena->committed = committed;
    arena->pos = ARENA_HEADER_SIZE;
    arena->commit_chunk = commit_chunk;
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
    OsMemRelease(base, reserved);
}

b32 ArenaIsNil(Arena *arena)
{
    return arena == 0 || arena->reserved == 0;
}

void *ArenaPush(Arena *arena, u64 size, u64 align)
{
    arena = ArenaOrNil(arena);
    void *result = ArenaPushRaw(arena, size, align);
    if (result == 0)
    {
        return ArenaFailedPush(size);
    }
    ArenaZeroBytes(result, size);
    return result;
}

void *ArenaPushNoZero(Arena *arena, u64 size, u64 align)
{
    arena = ArenaOrNil(arena);
    void *result = ArenaPushRaw(arena, size, align);
    if (result == 0)
    {
        return ArenaFailedPush(size);
    }
    return result;
}

b32 ArenaPushFailed(void *ptr)
{
    return ptr == 0 || ptr == arena_nil_block;
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

void ArenaPopTo(Arena *arena, u64 pos)
{
    arena = ArenaOrNil(arena);
    if (pos < ARENA_HEADER_SIZE)
    {
        pos = ARENA_HEADER_SIZE;
    }
    // Only moves backwards, so the nil arena (pos 0) is never written.
    if (pos < arena->pos)
    {
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
