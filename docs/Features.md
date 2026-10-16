This document will serve as the master feature list for this repository. Split into layers

## Architecture

- Enumerate CPU, compiler, and operating system.
- Enumerate SIMD capabilities.

## OS

- Unified platform layer
- Separate Windows, MacOS, Linux, iOS, Android implemtation layers below.

### Memory

- Reserve and Commit memory in one call
- Separate calls to reserve and commit memory if the OS allows for it.

### Filesystem

- Load files into memory.
- Write files to disk.
- List directory files.

### Windowing and input

- Create and resize a native window
- Process keyboard and mouse input

### Audio

- (note): I'm pretty foggy on audio generally
- Load and play audio files.

### Threading

- Functions to enable multithreaded code

## Containers

- Commonly used Data structures
- Arena memory allocator
- Small Growable Array
- Array Slice into arena
- String container and string operators
- Fast hash table
- Basic Queue and Stack

## Math

- Common math needed for game development
- Maximal focus on performance
- Trigonometry
- Rotations and Quaternions
- Matrices and Vectors and their operations
