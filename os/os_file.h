/*
    os_file.h

    Files and directories: whole-file read / write / append, file handles
    with reads and writes at an offset, file info, directory listing,
    create / delete / rename, a few well-known directories, and path helpers.

    Paths are UTF-8 String8s. Either '/' or '\' works as a separator; joins
    use the platform's own (OS_PATH_SEPARATOR). Long paths work without any
    manifest setting: the backend switches to the extended-length form when
    a path reaches MAX_PATH.

    Errors follow the base policy (docs/Conventions.md):
    - A failing call returns its ZII value (empty String8, nil OsFile, empty
      slice, false, 0) and reports through os_file_failure_hook, which the
      log layer turns into a warning with the path and the OS's reason:
          OsFile read "C:\game\data\x.png" failed: The system cannot find
          the file specified.
    - Callers that branch on why pass an OsFileError pointer as the last
      argument; everyone else passes 0. It is set to OsFileError_None on
      success.
    - The queries OsFileGetInfo / OsFileExists never report "not found",
      since that is a normal answer for them.

    OsFileWrite is atomic: it writes "<path>.tmp-<pid>-<thread>" and renames
    it over path, so a crash mid-save leaves the old file intact. NOTE: it
    does not flush to disk, so it is crash-safe but not power-loss-safe.
    TODO: an opt-in flush for save games.

    Arena results (OsFileRead, directory names, special directories) follow
    the String8 rule: 0-terminated after size. Path helpers other than
    OsPathJoin return views.

    Handles: the zero OsFile is the nil file; every call on it does nothing
    and returns 0. Handles are opened sharing read, write and delete, so
    tools can replace a file while a game holds it. ReadAt / WriteAt take an
    explicit offset and keep no file position, so several threads can read
    one handle at once.

    NOTE: File parsers (images, meshes, ...) do not belong here. They take
    bytes already in memory, e.g. the String8 from OsFileRead.

    TODO: POSIX backend (open / pread / pwrite / opendir / rename / mkdir).
    TODO: Memory-mapped files, and directory change notifications for hot
    reload.
*/

#pragma once

#include "../architecture/arch_context.h"
#include "../containers/arena.h"
#include "../containers/slice.h"
#include "../containers/string8.h"
#include "../types/base_types.h"

#if OS_WINDOWS
#define OS_PATH_SEPARATOR '\\'
#else
#define OS_PATH_SEPARATOR '/'
#endif

enum OsFileError
{
    OsFileError_None,
    OsFileError_NotFound,      // file, or a directory on the way to it
    OsFileError_AccessDenied,  // permissions, read-only, or it is a directory
    OsFileError_InUse,         // another process holds it without sharing
    OsFileError_AlreadyExists, // e.g. a file where a directory was wanted
    OsFileError_NotEmpty,      // deleting a directory that has entries
    OsFileError_Other,
    OsFileError_Count,
};

const char *OsFileErrorName(OsFileError error);

// operation names the call ("read", "write", "list", ...); path is empty for
// handle calls; os_error is the OS error code, 0 if there is none.
typedef void OsFileFailureHookFunction(const char *operation,
                                       String8 path,
                                       u32 os_error);

inline OsFileFailureHookFunction *os_file_failure_hook = 0;

// Whole files
String8 OsFileRead(Arena *arena, String8 path, OsFileError *error);
b32 OsFileWrite(String8 path, String8 data, OsFileError *error);
b32 OsFileAppend(String8 path, String8 data, OsFileError *error);

// Handles
struct OsFile
{
    u64 handle; // 0 = nil
};

enum OsFileOpenFlags : u32
{
    OsFileOpen_Read = 1 << 0,
    OsFileOpen_Write = 1 << 1,
    OsFileOpen_Create = 1 << 2,   // create if missing
    OsFileOpen_Truncate = 1 << 3, // empty it on open (needs Write)
};

OsFile OsFileOpen(String8 path, u32 flags, OsFileError *error);
void OsFileClose(OsFile file);
u64 OsFileSize(OsFile file);
// Bytes read; fewer than size at the end of the file.
u64 OsFileReadAt(OsFile file, u64 offset, void *buffer, u64 size);
// Bytes written; the file grows as needed.
u64 OsFileWriteAt(OsFile file, u64 offset, String8 data);

// Info
enum OsFileInfoFlags : u32
{
    OsFileInfo_Directory = 1 << 0,
    OsFileInfo_ReadOnly = 1 << 1,
};

struct OsFileInfo
{
    u64 size;
    u64 modified_ns; // nanoseconds since 1970-01-01 UTC
    u32 flags;       // OsFileInfoFlags
};

// info may be null. False if the path does not exist (not reported).
b32 OsFileGetInfo(String8 path, OsFileInfo *info);
b32 OsFileExists(String8 path);

// Directories and moves
struct OsDirectoryEntry
{
    String8 name; // name only; OsPathJoin it with the directory
    OsFileInfo info;
};

// Entries in no particular order, without "." and "..".
Slice<OsDirectoryEntry> OsDirectoryList(Arena *arena,
                                        String8 path,
                                        OsFileError *error);
// Creates missing parents too. An existing directory is success.
b32 OsDirectoryCreate(String8 path, OsFileError *error);
// The directory must be empty (else OsFileError_NotEmpty).
b32 OsDirectoryDelete(String8 path, OsFileError *error);
b32 OsFileDelete(String8 path, OsFileError *error);
// Replaces to if it exists.
b32 OsFileRename(String8 from, String8 to, OsFileError *error);

String8 OsExecutableDirectory(Arena *arena);
String8 OsCurrentDirectory(Arena *arena);

// Path helpers (pure string work)
String8 OsPathJoin(Arena *arena, String8 a, String8 b);
String8 OsPathFileName(String8 path);  // "dir/a.tar.gz" -> "a.tar.gz"
String8 OsPathExtension(String8 path); // "dir/a.tar.gz" -> "gz"
String8 OsPathDirectory(String8 path); // "dir/a.tar.gz" -> "dir"
