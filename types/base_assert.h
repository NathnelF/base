/*
    base_assert.h

    ASSERT(condition) breaks into the debugger when condition is false. It is
    active unless NDEBUG is defined; with NDEBUG it compiles to nothing and the
    condition is not evaluated, so never put side effects inside it.

    Release builds keep running after a would-be assert. Code that asserts
    should still fall back to a ZII-safe result (a nil struct, an empty
    slice) so the release path stays well defined.
*/

#pragma once

#include "../architecture/arch_context.h"

#if COMPILER_MSVC
#define ASSERT_BREAK() __debugbreak()
#elif COMPILER_CLANG
#define ASSERT_BREAK() __builtin_debugtrap()
#else
#define ASSERT_BREAK() __builtin_trap()
#endif

#if defined(NDEBUG)
#define ASSERT(condition) ((void)0)
#else
#define ASSERT(condition)                                                      \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            ASSERT_BREAK();                                                    \
        }                                                                      \
    } while (0)
#endif
