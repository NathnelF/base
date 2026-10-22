This document will serve as the master feature list for this repository. Split into layers

(Claude) notes: bullets marked "(Claude) debug:" are the error handling and debugging plans for that feature, from the error handling design. Each lands together with its feature and follows the policy in Conventions.md (asserts for bugs, ZII value + logged reason for runtime failures).

## Architecture

- Enumerate CPU, compiler, and operating system.
- Enumerate SIMD capabilities.

## OS

- Unified platform layer
- Separate Windows, MacOS, Linux, iOS, Android implemtation layers below.

### Memory

- Reserve and Commit memory in one call - implemented
- Separate calls to reserve and commit memory if the OS allows for it. - implemented

### Console

- stdout / stderr writes, printf-style Print, stdin line reads - implemented
- Buffered stdout with explicit flush

### Filesystem

- Load files into memory. - implemented
- Write files to disk. - implemented (atomic replace, append)
- List directory files. - implemented
- File handles with reads / writes at an offset, file info (size, modified time), create / delete / rename, executable and current directory, path helpers - implemented
- (Claude) debug: every failure returns a ZII value (empty String8, nil file handle, empty list) and logs a warning with the path and the OS message. - implemented
- (Claude) debug: a small reason enum (not found / access denied / in use / already exists / not empty / other) through an optional out-param for callers that branch on why it failed. - implemented
- Memory-mapped files
- Directory change notifications for hot reload
- Opt-in flush to disk for save files (OsFileWrite is crash-safe, not power-loss-safe)
- User data directory (%APPDATA% / ~/Library / XDG) for saves and settings
- (Claude) debug: a file sink for the log, so a crash leaves a log file next to the minidump.

### Windowing and input

- Create and resize a native window
- Process keyboard and mouse input
- (Claude) debug: failing to create the window or graphics device is a FATAL_ERROR (log, crash report, exit), since nothing can run without it.
- (Claude) debug: a lost or reset device logs a warning and recovers instead of failing.

### Audio

- (note): I'm pretty foggy on audio generally
- Load and play audio files.
- (Claude) debug: no audio device is a logged warning and audio becomes a no-op (a nil device), so the game still runs silent.
- (Claude) debug: decoding a bad audio file returns an empty sound and logs the file path and the reason.

### Threading

- Functions to enable multithreaded code
- (Claude) debug: name threads (SetThreadDescription) so debuggers and crash reports show them.
- (Claude) debug: a per-thread scratch arena and log scratch, released at thread exit (log.cpp has the TODO).
- (Claude) debug: debug-only asserts for lock misuse (unlocking a lock you do not own, recursive locking).
- (Claude) debug: SetThreadStackGuarantee on every thread base creates, so a stack overflow on any thread still produces a crash report.

## Containers

- Commonly used Data structures
- Arena memory allocator - implemented
- Small Growable Array
- (Claude) debug: ASSERT on out-of-bounds access with a nil-element fallback, like Slice; growth that fails keeps the old contents and logs through the arena hook.
- Array Slice into arena - implemented
- String container and string operators - implemented
- Fast hash table
- (Claude) debug: debug stats (load factor, longest probe) and a natvis view; ASSERT on misuse, ZII on capacity failure.
- (Claude) debug: replace Str8Hash's FNV-1a with a faster hash before the table relies on it.
- Basic Queue and Stack
- (Claude) debug: ASSERT on pop from empty in debug, and return a zeroed element (ZII) so release keeps running.

## Math

- Common math needed for game development
- Maximal focus on performance
- Trigonometry
- Rotations and Quaternions
- Matrices and Vectors and their operations
- (Claude) debug: ASSERT_FINITE in debug on inputs and outputs of matrix, quaternion and normalize functions, so a NaN is caught where it is produced.
- (Claude) debug: natvis views for vectors, matrices and quaternions.

## Error Handling and Debugging

- Error policy: asserts for bugs, ZII value + logged reason for runtime failures, reason enums only where callers branch (see Conventions.md) - implemented
- Asserts with messages, a hook, release-on variants (ASSERT_ALWAYS), INVALID_PATH and NOT_IMPLEMENTED - implemented
- Logging with levels compiled out per config, clickable file(line) output to the debugger and stderr - implemented
- Log scopes to capture an operation's messages as data - implemented
- OS error codes to readable messages - implemented
- Arena failures reported through the log instead of failing silently - implemented
- Arena debugging: 0xDD fill on pop, AddressSanitizer poisoning, peak usage - implemented
- Debugger visualizers (base.natvis) for String8, String16, Slice, String8List, Arena - implemented
- Crash handling: unhandled exception filter with stack trace and minidump; FATAL_ERROR; abort / invalid parameter / pure call routed through it; stack overflow reported - implemented
- (Claude) debug: include the last few log lines in crash reports (ring buffer)
- File log sink
- ASan build config in fbuild.bff


## Profiling

- System to profile performance
- Profile scopes
- (Claude moved from error handling) Profiling instrumentation (TIME_BLOCK / TIME_FUNCTION, rdtsc) for the profile config, output as Chrome trace JSON

