Platform layer for various operating systems.

Windows <- initial focus
MacOS
Linux

The layer is split around containers (see os.cpp): os_memory.cpp is
included before containers, os.cpp (everything needing String8 / Arena)
after.

Files:
- os.h: umbrella header.
- os_memory.h / os_memory.cpp: reserve / commit / decommit / release API,
  shared rounding, cached page info, ZII guards. Lower unity part.
- os_memory_win32.cpp: VirtualAlloc backend. POSIX backend is a TODO.
- os.cpp: upper unity part.
- os_debug.h / os_debug.cpp: OS error messages, debugger and stderr output.
- os_debug_win32.cpp: FormatMessageW / OutputDebugStringW / WriteConsoleW.
- os_crash.h / os_crash.cpp: crash handler, OsCrashRaise, OsStackTraceHere.
- os_crash_win32.cpp: exception filter, StackWalk64 / MiniDumpWriteDump via
  dbghelp loaded at runtime, CRT abort / invalid parameter / purecall hooks.
- test/os_crash_test.cpp: `fbuild os-crash-test-debug`; crashes child copies
  of itself and checks the reports.
- test/os_memory_test.cpp: `fbuild os-memory-test-debug`.
