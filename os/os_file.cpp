/*
    os_file.cpp

    Platform-independent half of os_file.h: error names, the failure
    reporting helper the backend uses, and the path helpers. Everything that
    touches the OS is in the backend included at the end.

    Path helper conventions: '/' and '\' are both separators. A trailing
    separator means "directory": OsPathFileName("a/b/") is empty and
    OsPathDirectory("a/b/") is "a/b". A leading dot is part of the name, not
    an extension: OsPathExtension(".gitignore") is empty.
*/

#include "os_file.h"

const char *OsFileErrorName(OsFileError error)
{
    switch (error)
    {
        case OsFileError_None:
            return "none";
        case OsFileError_NotFound:
            return "not found";
        case OsFileError_AccessDenied:
            return "access denied";
        case OsFileError_InUse:
            return "in use";
        case OsFileError_AlreadyExists:
            return "already exists";
        case OsFileError_NotEmpty:
            return "not empty";
        default:
            return "other";
    }
}

static void OsFileSetError(OsFileError *out, OsFileError error)
{
    if (out != 0)
    {
        *out = error;
    }
}

static void OsFileReport(const char *operation,
                         String8 path,
                         u32 os_error,
                         OsFileError error,
                         OsFileError *error_out)
{
    OsFileSetError(error_out, error);
    if (os_file_failure_hook != 0)
    {
        os_file_failure_hook(operation, path, os_error);
    }
}

static b32 OsPathIsSeparator(u8 c)
{
    return c == '/' || c == '\\';
}

// Index of the last separator, or path.size if there is none.
static u64 OsPathLastSeparator(String8 path)
{
    for (u64 i = path.size; i > 0; --i)
    {
        if (OsPathIsSeparator(path.data[i - 1]))
        {
            return i - 1;
        }
    }
    return path.size;
}

static String8 OsPathTrimTrailingSeparators(String8 path)
{
    u64 size = path.size;
    while (size > 0 && OsPathIsSeparator(path.data[size - 1]))
    {
        --size;
    }
    return Str8Prefix(path, size);
}

String8 OsPathJoin(Arena *arena, String8 a, String8 b)
{
    String8 head = OsPathTrimTrailingSeparators(a);
    u64 skip = 0;
    while (skip < b.size && OsPathIsSeparator(b.data[skip]))
    {
        ++skip;
    }
    String8 tail = Str8Skip(b, skip);
    if (head.size == 0)
    {
        return Str8Copy(arena, b);
    }
    if (tail.size == 0)
    {
        return Str8Copy(arena, a);
    }
    u8 separator = OS_PATH_SEPARATOR;
    return Str8PushF(arena, "%S%c%S", head, separator, tail);
}

String8 OsPathFileName(String8 path)
{
    u64 separator = OsPathLastSeparator(path);
    if (separator == path.size)
    {
        return path;
    }
    return Str8Skip(path, separator + 1);
}

String8 OsPathExtension(String8 path)
{
    String8 name = OsPathFileName(path);
    u64 dot = Str8FindLast(name, STR8_LIT("."));
    String8 empty = {};
    if (dot == name.size || dot == 0)
    {
        return empty;
    }
    return Str8Skip(name, dot + 1);
}

String8 OsPathDirectory(String8 path)
{
    u64 separator = OsPathLastSeparator(path);
    String8 empty = {};
    if (separator == path.size)
    {
        return empty;
    }
    return Str8Prefix(path, separator);
}

#include "os_file_win32.cpp"

#if !OS_WINDOWS
#error "os_file: no backend for this OS yet"
#endif
