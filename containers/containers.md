Containers and data structures used across the repository.

Files:
- containers.h / containers.cpp: umbrella header and layer unity file.
- arena.h / arena.cpp: ZII reserve-and-commit-on-demand arena.
- slice.h: header-only Slice<T> view (pointer + count).
- test/arena_test.cpp: `fbuild arena-test-debug`.
- test/slice_test.cpp: `fbuild slice-test-debug`.
