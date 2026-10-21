/*
    base_assert.h

    Asserts are for programmer bugs (broken contracts), never for runtime
    failures like a missing file; those return a ZII value and log instead.

    ASSERT(condition)              debug only
    ASSERT_MSG(condition, message) debug only, with a message
    ASSERT_ALWAYS(condition)       every config; for cheap checks that guard
    ASSERT_ALWAYS_MSG(c, message)  memory safety
    INVALID_PATH                   debug only; code that should be unreachable
    NOT_IMPLEMENTED                debug only; a stub that was hit
    ASSERT_FAILED_MSG(message)     debug only; reached a path that is always
                                   a bug, e.g. after detecting bad input

    The debug-only forms compile to nothing when NDEBUG is defined and the
    condition is not evaluated, so never put side effects inside them.
    Release builds keep running past a would-be ASSERT, so code that asserts
    must still fall back to a ZII-safe result (a nil struct, an empty slice).

    On failure every form calls base_assert_hook (when set) with the file,
    line, expression text and message, then breaks into the debugger if the
    hook returns true (or there is no hook). The log layer installs a hook
    that prints "file(line): assert: ..." first. Tests install one that
    counts failures and returns false to keep running.

    NOTE: Without a debugger attached the break is an unhandled breakpoint
    exception, so a failed ASSERT_ALWAYS in release ends the process. That is
    intended: it only guards states that are not safe to continue from.

    NOTE: base_assert_hook is a plain global. Set it once at startup, before
    other threads run.
*/

#pragma once

#include "../architecture/arch_context.h"
#include "base_types.h"

#if COMPILER_MSVC
#define ASSERT_BREAK() __debugbreak()
#elif COMPILER_CLANG
#define ASSERT_BREAK() __builtin_debugtrap()
#else
#define ASSERT_BREAK() __builtin_trap()
#endif

// Returns true to break into the debugger, false to continue.
typedef b32 AssertHookFunction(const char *file,
                               int line,
                               const char *expression,
                               const char *message);

inline AssertHookFunction *base_assert_hook = 0;

inline b32 AssertFailed(const char *file,
                        int line,
                        const char *expression,
                        const char *message)
{
    if (base_assert_hook != 0)
    {
        return base_assert_hook(file, line, expression, message);
    }
    return 1;
}

#define ASSERT_ALWAYS_MSG(condition, message)                                  \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            if (AssertFailed(__FILE__, __LINE__, #condition, (message)))       \
            {                                                                  \
                ASSERT_BREAK();                                                \
            }                                                                  \
        }                                                                      \
    } while (0)

#define ASSERT_ALWAYS(condition) ASSERT_ALWAYS_MSG(condition, "")

#if defined(NDEBUG)
#define ASSERT(condition) ((void)0)
#define ASSERT_MSG(condition, message) ((void)0)
#define INVALID_PATH ((void)0)
#define NOT_IMPLEMENTED ((void)0)
#define ASSERT_FAILED_MSG(message) ((void)0)
#else
#define ASSERT(condition) ASSERT_ALWAYS_MSG(condition, "")
#define ASSERT_MSG(condition, message) ASSERT_ALWAYS_MSG(condition, message)
// These call AssertFailed directly; ASSERT(0) would trip "conditional
// expression is constant" warnings at /W4.
#define INVALID_PATH                                                           \
    do                                                                         \
    {                                                                          \
        if (AssertFailed(__FILE__, __LINE__, "INVALID_PATH", "unreachable"))   \
        {                                                                      \
            ASSERT_BREAK();                                                    \
        }                                                                      \
    } while (0)
#define NOT_IMPLEMENTED                                                        \
    do                                                                         \
    {                                                                          \
        if (AssertFailed(                                                      \
              __FILE__, __LINE__, "NOT_IMPLEMENTED", "not implemented"))       \
        {                                                                      \
            ASSERT_BREAK();                                                    \
        }                                                                      \
    } while (0)
#define ASSERT_FAILED_MSG(message)                                             \
    do                                                                         \
    {                                                                          \
        if (AssertFailed(__FILE__, __LINE__, "ASSERT_FAILED", (message)))      \
        {                                                                      \
            ASSERT_BREAK();                                                    \
        }                                                                      \
    } while (0)
#endif
