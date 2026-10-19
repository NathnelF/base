Platform layer for various operating systems.

Windows <- initial focus
MacOS
Linux

Files:
- os.h / os.cpp: umbrella header and layer unity file.
- os_memory.h: reserve / commit / decommit / release API.
- os_memory.cpp: shared rounding, cached page info, ZII guards.
- os_memory_win32.cpp: VirtualAlloc backend. POSIX backend is a TODO.
- test/os_memory_test.cpp: `fbuild os-memory-test-debug`.
