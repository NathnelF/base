Containers and data structures used across the repository.

Files:
- containers.h / containers.cpp: umbrella header and layer unity file.
- arena.h / arena.cpp: ZII reserve-and-commit-on-demand arena.
- slice.h: header-only Slice<T> view (pointer + count).
- string8.h / string8.cpp: String8 views, lists, parsing, UTF-8/16.
- string8_format.cpp: Str8PushF printf-style formatting.
- test/arena_test.cpp: `fbuild arena-test-debug`.
- test/slice_test.cpp: `fbuild slice-test-debug`.
- test/string_test.cpp: `fbuild string-test-debug`.
