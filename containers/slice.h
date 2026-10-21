/*
    slice.h

    Slice<T>: a typed view of count elements starting at data. It owns
    nothing. Arena memory never moves, so a slice into an arena stays valid
    until that arena is popped past it or released. A slice can view any
    other memory too.

    ZII: the zero slice is a valid empty slice. Nothing here returns a slice
    into the arena's shared nil scratch; a failed allocation gives an empty
    slice instead, so writes through a slice always land in real memory.

    Indexing out of bounds ASSERTs. With asserts off it returns a reference to
    a zeroed per-type nil element. The nil element is re-zeroed on every
    out-of-bounds access, is shared and writable, and should never be relied
    on (same rules as the arena's nil scratch).

    NOTE: operator[] is the one method in base; C++ requires it to be a
    member. begin and end are lowercase free functions because range-for
    looks them up by those names.
*/

#pragma once

#include "../types/base_assert.h"
#include "../types/base_memory.h"
#include "../types/base_types.h"
#include "arena.h"

template<typename T>
inline T slice_nil_element = {};

template<typename T>
struct Slice
{
    T *data;
    u64 count;

    T &operator[](u64 index)
    {
        ASSERT(index < count);
        if (index < count)
        {
            return data[index];
        }
        slice_nil_element<T> = {};
        return slice_nil_element<T>;
    }
};

template<typename T>
Slice<T> SliceMake(T *data, u64 count)
{
    Slice<T> result = {};
    if (data != 0)
    {
        result.data = data;
        result.count = count;
    }
    return result;
}

// count zeroed elements from the arena. Empty slice on failure or count 0.
template<typename T>
Slice<T> SlicePush(Arena *arena, u64 count)
{
    Slice<T> result = {};
    if (count == 0)
    {
        return result;
    }
    T *data = ArenaPushArray<T>(arena, count);
    if (!ArenaPushFailed(data))
    {
        result.data = data;
        result.count = count;
    }
    return result;
}

template<typename T>
Slice<T> SlicePushNoZero(Arena *arena, u64 count)
{
    Slice<T> result = {};
    if (count == 0)
    {
        return result;
    }
    T *data = ArenaPushArrayNoZero<T>(arena, count);
    if (!ArenaPushFailed(data))
    {
        result.data = data;
        result.count = count;
    }
    return result;
}

// A new slice in arena holding a copy of source's elements.
template<typename T>
Slice<T> SliceCopy(Arena *arena, Slice<T> source)
{
    Slice<T> result = SlicePushNoZero<T>(arena, source.count);
    MemCopy(result.data, source.data, sizeof(T) * result.count);
    return result;
}

// Elements [start, end), both clamped to count.
template<typename T>
Slice<T> SliceSub(Slice<T> slice, u64 start, u64 end)
{
    if (end > slice.count)
    {
        end = slice.count;
    }
    Slice<T> result = {};
    if (start < end)
    {
        result.data = slice.data + start;
        result.count = end - start;
    }
    return result;
}

// The first count elements.
template<typename T>
Slice<T> SlicePrefix(Slice<T> slice, u64 count)
{
    return SliceSub(slice, 0, count);
}

// Everything after the first count elements.
template<typename T>
Slice<T> SliceSkip(Slice<T> slice, u64 count)
{
    return SliceSub(slice, count, slice.count);
}

template<typename T>
T *begin(Slice<T> slice)
{
    return slice.data;
}

template<typename T>
T *end(Slice<T> slice)
{
    return slice.data + slice.count;
}
