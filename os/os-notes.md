Platform layer for various operating systems.

Windows <- initial focus
MacOS
Linux

The layer is split around containers (see os_upper.cpp): os_lower.cpp is
included before containers, os_upper.cpp (everything needing String8 /
Arena) after.

Files:
- os.h: umbrella header.
- os_lower.cpp: lower unity part (before containers); includes os_memory.cpp.
- os_memory.h / os_memory.cpp: reserve / commit / decommit / release API,
  shared rounding, cached page info, ZII guards.
- os_memory_win32.cpp: VirtualAlloc backend. POSIX backend is a TODO.
- os_upper.cpp: upper unity part (after containers); includes the rest.
- os_debug.h / os_debug.cpp: OS error messages, debugger output.
- os_debug_win32.cpp: FormatMessageW / OutputDebugStringW, and the UTF-16
  chunked writer os_console reuses.
- os_console.h / os_console.cpp: stdout / stderr / stdin, Print.
- os_console_win32.cpp: WriteConsoleW / ReadConsoleW for consoles,
  WriteFile / ReadFile for redirected streams.
- os_file.h / os_file.cpp: files, handles, info, directories, path helpers.
- os_file_win32.cpp: CreateFileW / FindFirstFileExW / MoveFileExW backend,
  long-path (\\?\ prefix) handling, per-thread scratch for path conversion.
- os_crash.h / os_crash.cpp: crash handler, OsCrashRaise, OsStackTraceHere.
- os_crash_win32.cpp: exception filter, StackWalk64 / MiniDumpWriteDump via
  dbghelp loaded at runtime, CRT abort / invalid parameter / purecall hooks.
- test/os_crash_test.cpp: `fbuild os-crash-test-debug`; crashes child copies
  of itself and checks the reports.
- test/os_console_test.cpp: `fbuild os-console-test-debug`; runs child
  copies of itself with redirected stdin / stdout.
- test/os_file_test.cpp: `fbuild os-file-test-debug`; works in a sandbox
  folder next to the exe.
- test/os_memory_test.cpp: `fbuild os-memory-test-debug`.
