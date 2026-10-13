#pragma once

#include <stdint.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef i8 b8;
typedef i32 b32;

typedef intptr_t iptr;
typedef uintptr_t uptr;

static_assert(sizeof(i8) == 1, "i8 size");
static_assert(sizeof(i16) == 2, "i16 size");
static_assert(sizeof(i32) == 4, "i32 size");
static_assert(sizeof(i64) == 8, "i64 size");
static_assert(sizeof(f32) == 4, "f32 size");
static_assert(sizeof(f64) == 8, "f64 size");
