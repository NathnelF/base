/*
    os_file_win32.cpp

    Windows backend for os_file.h. Included by os_file.cpp.

    Every call converts its UTF-8 path to UTF-16 in a per-thread scratch
    arena inside an ArenaTemp, so path handling never leaks memory. Paths of
    MAX_PATH - 12 characters or more (the CreateDirectoryW limit) are made
    absolute with GetFullPathNameW and given the \\?\ prefix (\\?\UNC\ for
    network paths), which lifts the limit to about 32K characters.
    TODO: use the general per-thread scratch arenas once threading lands.

    The OS error code is read with GetLastError immediately after the
    failing call, before any cleanup can overwrite it.

    ReadFile / WriteFile take a 32-bit count, so transfers loop in chunks of
    OS_WIN32_FILE_CHUNK. ReadAt / WriteAt pass the offset in an OVERLAPPED on
    a synchronous handle, which reads at that offset without touching any
    shared file position.
*/

#if OS_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

constexpr u64 OS_WIN32_FILE_SCRATCH_RESERVE = 256ull * 1024 * 1024;
constexpr u64 OS_WIN32_FILE_CHUNK = 1ull << 30;
constexpr u64 OS_WIN32_FILETIME_UNIX_EPOCH = 116444736000000000ull;
constexpr DWORD OS_WIN32_FILE_SHARE_ALL =
  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

static thread_local Arena *os_win32_file_scratch;

static Arena *OsWin32FileScratch(void)
{
    if (os_win32_file_scratch == 0)
    {
        os_win32_file_scratch = ArenaAlloc(OS_WIN32_FILE_SCRATCH_RESERVE);
    }
    return os_win32_file_scratch;
}

static OsFileError OsWin32FileErrorFromCode(DWORD code)
{
    switch (code)
    {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_DRIVE:
        case ERROR_BAD_NETPATH:
        case ERROR_BAD_NET_NAME:
            return OsFileError_NotFound;
        case ERROR_ACCESS_DENIED:
        case ERROR_WRITE_PROTECT:
            return OsFileError_AccessDenied;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return OsFileError_InUse;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:
            return OsFileError_AlreadyExists;
        case ERROR_DIR_NOT_EMPTY:
            return OsFileError_NotEmpty;
        default:
            return OsFileError_Other;
    }
}

// Reports the failure described by code (already read from GetLastError).
static void OsWin32FileFail(const char *operation,
                            String8 path,
                            DWORD code,
                            OsFileError *error)
{
    OsFileReport(
      operation, path, (u32)code, OsWin32FileErrorFromCode(code), error);
}

static b32 OsWin32WideStartsWith(u16 *text, u64 size, const wchar_t *prefix)
{
    u64 i = 0;
    while (prefix[i] != 0)
    {
        if (i >= size || text[i] != (u16)prefix[i])
        {
            return 0;
        }
        ++i;
    }
    return 1;
}

// UTF-16, 0-terminated, in scratch. Long paths get the \\?\ form.
static wchar_t *OsWin32WidePath(Arena *scratch, String8 path)
{
    String16 wide = Str16From8(scratch, path);
    if (wide.data == 0)
    {
        return (wchar_t *)L"";
    }
    if (wide.size < MAX_PATH - 12 ||
        OsWin32WideStartsWith(wide.data, wide.size, L"\\\\?\\"))
    {
        return (wchar_t *)wide.data;
    }
    DWORD needed = GetFullPathNameW((wchar_t *)wide.data, 0, 0, 0);
    if (needed == 0)
    {
        return (wchar_t *)wide.data;
    }
    // Room for "\\?\UNC\" (8) in front of the full path.
    u16 *buffer = ArenaPushArrayNoZero<u16>(scratch, (u64)needed + 8);
    if (ArenaPushFailed(buffer))
    {
        return (wchar_t *)wide.data;
    }
    u16 *full = buffer + 8;
    DWORD length =
      GetFullPathNameW((wchar_t *)wide.data, needed, (wchar_t *)full, 0);
    if (length == 0 || length >= needed)
    {
        return (wchar_t *)wide.data;
    }
    if (OsWin32WideStartsWith(full, length, L"\\\\"))
    {
        // \\server\share\x -> \\?\UNC\server\share\x
        u16 *start = full - 6;
        const wchar_t *prefix = L"\\\\?\\UNC";
        for (u64 i = 0; i < 7; ++i)
        {
            start[i] = (u16)prefix[i];
        }
        start[7] = '\\';
        return (wchar_t *)start;
    }
    u16 *start = full - 4;
    start[0] = '\\';
    start[1] = '\\';
    start[2] = '?';
    start[3] = '\\';
    return (wchar_t *)start;
}

static u64 OsWin32FileTimeToUnixNs(FILETIME time)
{
    u64 ticks = ((u64)time.dwHighDateTime << 32) | time.dwLowDateTime;
    if (ticks < OS_WIN32_FILETIME_UNIX_EPOCH)
    {
        return 0;
    }
    return (ticks - OS_WIN32_FILETIME_UNIX_EPOCH) * 100;
}

static OsFileInfo OsWin32FileInfoFrom(DWORD attributes,
                                      DWORD size_high,
                                      DWORD size_low,
                                      FILETIME modified)
{
    OsFileInfo info = {};
    info.size = ((u64)size_high << 32) | size_low;
    info.modified_ns = OsWin32FileTimeToUnixNs(modified);
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        info.flags |= OsFileInfo_Directory;
        info.size = 0;
    }
    if (attributes & FILE_ATTRIBUTE_READONLY)
    {
        info.flags |= OsFileInfo_ReadOnly;
    }
    return info;
}

// Writes all of data at the handle's current position. Returns the error
// code, 0 on success.
static DWORD OsWin32WriteAll(HANDLE handle, String8 data)
{
    u64 offset = 0;
    while (offset < data.size)
    {
        u64 chunk = data.size - offset;
        if (chunk > OS_WIN32_FILE_CHUNK)
        {
            chunk = OS_WIN32_FILE_CHUNK;
        }
        DWORD written = 0;
        if (!WriteFile(handle, data.data + offset, (DWORD)chunk, &written, 0))
        {
            return GetLastError();
        }
        offset += written;
    }
    return 0;
}

String8 OsFileRead(Arena *arena, String8 path, OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    String8 result = {};
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);

    HANDLE handle =
      CreateFileW(OsWin32WidePath(scratch, path),
                  GENERIC_READ,
                  OS_WIN32_FILE_SHARE_ALL,
                  0,
                  OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                  0);
    if (handle == INVALID_HANDLE_VALUE)
    {
        OsWin32FileFail("read", path, GetLastError(), error);
        ArenaTempEnd(temp);
        return result;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(handle, &size))
    {
        DWORD code = GetLastError();
        CloseHandle(handle);
        OsWin32FileFail("read", path, code, error);
        ArenaTempEnd(temp);
        return result;
    }

    u64 file_size = (u64)size.QuadPart;
    u8 *data = ArenaPushArrayNoZero<u8>(arena, file_size + 1);
    if (ArenaPushFailed(data))
    {
        CloseHandle(handle);
        OsFileReport("read (out of memory)", path, 0, OsFileError_Other, error);
        ArenaTempEnd(temp);
        return result;
    }

    u64 total = 0;
    while (total < file_size)
    {
        u64 chunk = file_size - total;
        if (chunk > OS_WIN32_FILE_CHUNK)
        {
            chunk = OS_WIN32_FILE_CHUNK;
        }
        DWORD got = 0;
        if (!ReadFile(handle, data + total, (DWORD)chunk, &got, 0))
        {
            DWORD code = GetLastError();
            CloseHandle(handle);
            OsWin32FileFail("read", path, code, error);
            ArenaTempEnd(temp);
            return result;
        }
        if (got == 0)
        {
            break;
        }
        total += got;
    }
    CloseHandle(handle);
    if (total < file_size)
    {
        // Not an error for the caller: they get what is there now.
        if (os_file_failure_hook != 0)
        {
            os_file_failure_hook("read (file shrank while reading)", path, 0);
        }
    }
    data[total] = 0;
    result.data = data;
    result.size = total;
    ArenaTempEnd(temp);
    return result;
}

b32 OsFileWrite(String8 path, String8 data, OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    b32 ok = 0;

    String8 temp_path = Str8PushF(scratch,
                                  "%S.tmp-%u-%u",
                                  path,
                                  (u32)GetCurrentProcessId(),
                                  (u32)GetCurrentThreadId());
    wchar_t *wide_temp = OsWin32WidePath(scratch, temp_path);
    wchar_t *wide_path = OsWin32WidePath(scratch, path);

    HANDLE handle = CreateFileW(
      wide_temp, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == INVALID_HANDLE_VALUE)
    {
        OsWin32FileFail("write", path, GetLastError(), error);
    }
    else
    {
        DWORD code = OsWin32WriteAll(handle, data);
        CloseHandle(handle);
        if (code != 0)
        {
            DeleteFileW(wide_temp);
            OsWin32FileFail("write", path, code, error);
        }
        else if (!MoveFileExW(wide_temp, wide_path, MOVEFILE_REPLACE_EXISTING))
        {
            code = GetLastError();
            DeleteFileW(wide_temp);
            OsWin32FileFail("write (replace)", path, code, error);
        }
        else
        {
            ok = 1;
        }
    }
    ArenaTempEnd(temp);
    return ok;
}

b32 OsFileAppend(String8 path, String8 data, OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    b32 ok = 0;

    HANDLE handle = CreateFileW(OsWin32WidePath(scratch, path),
                                FILE_APPEND_DATA,
                                OS_WIN32_FILE_SHARE_ALL,
                                0,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                0);
    if (handle == INVALID_HANDLE_VALUE)
    {
        OsWin32FileFail("append", path, GetLastError(), error);
    }
    else
    {
        DWORD code = OsWin32WriteAll(handle, data);
        CloseHandle(handle);
        if (code != 0)
        {
            OsWin32FileFail("append", path, code, error);
        }
        else
        {
            ok = 1;
        }
    }
    ArenaTempEnd(temp);
    return ok;
}

OsFile OsFileOpen(String8 path, u32 flags, OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    OsFile result = {};
    ASSERT_MSG((flags & (OsFileOpen_Read | OsFileOpen_Write)) != 0,
               "OsFileOpen: flags need OsFileOpen_Read or OsFileOpen_Write");
    ASSERT_MSG(!(flags & OsFileOpen_Truncate) || (flags & OsFileOpen_Write),
               "OsFileOpen: OsFileOpen_Truncate needs OsFileOpen_Write");

    DWORD access = 0;
    if (flags & OsFileOpen_Read)
    {
        access |= GENERIC_READ;
    }
    if (flags & OsFileOpen_Write)
    {
        access |= GENERIC_WRITE;
    }
    if (access == 0)
    {
        access = GENERIC_READ;
    }

    DWORD disposition = OPEN_EXISTING;
    b32 create = (flags & OsFileOpen_Create) != 0;
    b32 truncate =
      (flags & OsFileOpen_Truncate) != 0 && (access & GENERIC_WRITE);
    if (create && truncate)
    {
        disposition = CREATE_ALWAYS;
    }
    else if (create)
    {
        disposition = OPEN_ALWAYS;
    }
    else if (truncate)
    {
        disposition = TRUNCATE_EXISTING;
    }

    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    HANDLE handle = CreateFileW(OsWin32WidePath(scratch, path),
                                access,
                                OS_WIN32_FILE_SHARE_ALL,
                                0,
                                disposition,
                                FILE_ATTRIBUTE_NORMAL,
                                0);
    if (handle == INVALID_HANDLE_VALUE)
    {
        OsWin32FileFail("open", path, GetLastError(), error);
    }
    else
    {
        result.handle = (u64)handle;
    }
    ArenaTempEnd(temp);
    return result;
}

void OsFileClose(OsFile file)
{
    if (file.handle != 0)
    {
        CloseHandle((HANDLE)file.handle);
    }
}

u64 OsFileSize(OsFile file)
{
    if (file.handle == 0)
    {
        return 0;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx((HANDLE)file.handle, &size))
    {
        String8 no_path = {};
        OsWin32FileFail("size", no_path, GetLastError(), 0);
        return 0;
    }
    return (u64)size.QuadPart;
}

u64 OsFileReadAt(OsFile file, u64 offset, void *buffer, u64 size)
{
    if (file.handle == 0 || buffer == 0)
    {
        return 0;
    }
    u64 total = 0;
    while (total < size)
    {
        u64 chunk = size - total;
        if (chunk > OS_WIN32_FILE_CHUNK)
        {
            chunk = OS_WIN32_FILE_CHUNK;
        }
        u64 at = offset + total;
        OVERLAPPED overlapped = {};
        overlapped.Offset = (DWORD)at;
        overlapped.OffsetHigh = (DWORD)(at >> 32);
        DWORD got = 0;
        if (!ReadFile((HANDLE)file.handle,
                      (u8 *)buffer + total,
                      (DWORD)chunk,
                      &got,
                      &overlapped))
        {
            DWORD code = GetLastError();
            if (code != ERROR_HANDLE_EOF)
            {
                String8 no_path = {};
                OsWin32FileFail("read at", no_path, code, 0);
            }
            break;
        }
        if (got == 0)
        {
            break;
        }
        total += got;
    }
    return total;
}

u64 OsFileWriteAt(OsFile file, u64 offset, String8 data)
{
    if (file.handle == 0)
    {
        return 0;
    }
    u64 total = 0;
    while (total < data.size)
    {
        u64 chunk = data.size - total;
        if (chunk > OS_WIN32_FILE_CHUNK)
        {
            chunk = OS_WIN32_FILE_CHUNK;
        }
        u64 at = offset + total;
        OVERLAPPED overlapped = {};
        overlapped.Offset = (DWORD)at;
        overlapped.OffsetHigh = (DWORD)(at >> 32);
        DWORD written = 0;
        if (!WriteFile((HANDLE)file.handle,
                       data.data + total,
                       (DWORD)chunk,
                       &written,
                       &overlapped))
        {
            String8 no_path = {};
            OsWin32FileFail("write at", no_path, GetLastError(), 0);
            break;
        }
        total += written;
    }
    return total;
}

b32 OsFileGetInfo(String8 path, OsFileInfo *info)
{
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    b32 ok = GetFileAttributesExW(
      OsWin32WidePath(scratch, path), GetFileExInfoStandard, &data);
    if (!ok)
    {
        DWORD code = GetLastError();
        if (OsWin32FileErrorFromCode(code) != OsFileError_NotFound)
        {
            OsWin32FileFail("get info", path, code, 0);
        }
    }
    else if (info != 0)
    {
        *info = OsWin32FileInfoFrom(data.dwFileAttributes,
                                    data.nFileSizeHigh,
                                    data.nFileSizeLow,
                                    data.ftLastWriteTime);
    }
    ArenaTempEnd(temp);
    return ok;
}

b32 OsFileExists(String8 path)
{
    return OsFileGetInfo(path, 0);
}

struct OsWin32DirectoryNode
{
    OsWin32DirectoryNode *next;
    OsDirectoryEntry entry;
};

Slice<OsDirectoryEntry> OsDirectoryList(Arena *arena,
                                        String8 path,
                                        OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    Slice<OsDirectoryEntry> result = {};
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);

    String8 pattern = OsPathJoin(scratch, path, STR8_LIT("*"));
    WIN32_FIND_DATAW found = {};
    HANDLE find = FindFirstFileExW(OsWin32WidePath(scratch, pattern),
                                   FindExInfoBasic,
                                   &found,
                                   FindExSearchNameMatch,
                                   0,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        OsWin32FileFail("list", path, GetLastError(), error);
        ArenaTempEnd(temp);
        return result;
    }

    // Nodes go in scratch; names and the final slice go in arena.
    OsWin32DirectoryNode *first = 0;
    OsWin32DirectoryNode *last = 0;
    u64 count = 0;
    do
    {
        u64 length = 0;
        while (found.cFileName[length] != 0)
        {
            ++length;
        }
        String16 name16 = { (u16 *)found.cFileName, length };
        b32 is_dot = (length == 1 && found.cFileName[0] == '.') ||
                     (length == 2 && found.cFileName[0] == '.' &&
                      found.cFileName[1] == '.');
        if (!is_dot)
        {
            OsWin32DirectoryNode *node =
              ArenaPushStruct<OsWin32DirectoryNode>(scratch);
            if (ArenaPushFailed(node))
            {
                break;
            }
            node->entry.name = Str8From16(arena, name16);
            node->entry.info = OsWin32FileInfoFrom(found.dwFileAttributes,
                                                   found.nFileSizeHigh,
                                                   found.nFileSizeLow,
                                                   found.ftLastWriteTime);
            if (last != 0)
            {
                last->next = node;
            }
            else
            {
                first = node;
            }
            last = node;
            ++count;
        }
    } while (FindNextFileW(find, &found));

    DWORD code = GetLastError();
    FindClose(find);
    if (code != ERROR_NO_MORE_FILES && code != 0)
    {
        // Partial listing: report it, but return what was found.
        OsWin32FileFail("list", path, code, error);
    }

    result = SlicePushNoZero<OsDirectoryEntry>(arena, count);
    u64 index = 0;
    for (OsWin32DirectoryNode *node = first; node != 0 && index < result.count;
         node = node->next)
    {
        result.data[index] = node->entry;
        ++index;
    }
    ArenaTempEnd(temp);
    return result;
}

static b32 OsWin32IsDirectory(wchar_t *wide)
{
    DWORD attributes = GetFileAttributesW(wide);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Creates path and any missing parents. Returns 0 or the error code.
static DWORD OsWin32CreateDirectoryRecursive(Arena *scratch, String8 path)
{
    path = OsPathTrimTrailingSeparators(path);
    if (path.size == 0)
    {
        return 0;
    }
    wchar_t *wide = OsWin32WidePath(scratch, path);
    if (CreateDirectoryW(wide, 0))
    {
        return 0;
    }
    DWORD code = GetLastError();
    if (code == ERROR_PATH_NOT_FOUND)
    {
        String8 parent = OsPathDirectory(path);
        if (parent.size > 0 && parent.size < path.size)
        {
            DWORD parent_code =
              OsWin32CreateDirectoryRecursive(scratch, parent);
            if (parent_code != 0)
            {
                return parent_code;
            }
            if (CreateDirectoryW(wide, 0))
            {
                return 0;
            }
            code = GetLastError();
        }
    }
    // Already there (including drive roots, which refuse CreateDirectory).
    if (OsWin32IsDirectory(wide))
    {
        return 0;
    }
    return code;
}

b32 OsDirectoryCreate(String8 path, OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    DWORD code = OsWin32CreateDirectoryRecursive(scratch, path);
    if (code != 0)
    {
        OsWin32FileFail("create directory", path, code, error);
    }
    ArenaTempEnd(temp);
    return code == 0;
}

b32 OsDirectoryDelete(String8 path, OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    b32 ok = RemoveDirectoryW(OsWin32WidePath(scratch, path)) != 0;
    if (!ok)
    {
        OsWin32FileFail("delete directory", path, GetLastError(), error);
    }
    ArenaTempEnd(temp);
    return ok;
}

b32 OsFileDelete(String8 path, OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    b32 ok = DeleteFileW(OsWin32WidePath(scratch, path)) != 0;
    if (!ok)
    {
        OsWin32FileFail("delete", path, GetLastError(), error);
    }
    ArenaTempEnd(temp);
    return ok;
}

b32 OsFileRename(String8 from, String8 to, OsFileError *error)
{
    OsFileSetError(error, OsFileError_None);
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    b32 ok =
      MoveFileExW(OsWin32WidePath(scratch, from),
                  OsWin32WidePath(scratch, to),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
    if (!ok)
    {
        DWORD code = GetLastError();
        String8 both = Str8PushF(scratch, "%S\" -> \"%S", from, to);
        OsWin32FileFail("rename", both, code, error);
    }
    ArenaTempEnd(temp);
    return ok;
}

String8 OsExecutableDirectory(Arena *arena)
{
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    String8 result = {};
    // GetModuleFileNameW truncates silently; grow until it fits.
    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2)
    {
        u16 *buffer = ArenaPushArrayNoZero<u16>(scratch, capacity);
        if (ArenaPushFailed(buffer))
        {
            break;
        }
        DWORD length = GetModuleFileNameW(0, (wchar_t *)buffer, capacity);
        if (length == 0)
        {
            break;
        }
        if (length < capacity)
        {
            // Cut at the last separator: the directory, without the name.
            while (length > 0 && buffer[length - 1] != '\\' &&
                   buffer[length - 1] != '/')
            {
                --length;
            }
            if (length > 0)
            {
                --length;
            }
            String16 directory = { buffer, length };
            result = Str8From16(arena, directory);
            break;
        }
    }
    ArenaTempEnd(temp);
    return result;
}

String8 OsCurrentDirectory(Arena *arena)
{
    Arena *scratch = OsWin32FileScratch();
    ArenaTemp temp = ArenaTempBegin(scratch);
    String8 result = {};
    DWORD needed = GetCurrentDirectoryW(0, 0);
    u16 *buffer = ArenaPushArrayNoZero<u16>(scratch, (u64)needed + 1);
    if (needed > 0 && !ArenaPushFailed(buffer))
    {
        DWORD length = GetCurrentDirectoryW(needed, (wchar_t *)buffer);
        if (length > 0 && length < needed)
        {
            String16 directory = { buffer, length };
            result = Str8From16(arena, directory);
        }
    }
    ArenaTempEnd(temp);
    return result;
}

#endif
