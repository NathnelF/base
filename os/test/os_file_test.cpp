/*
    os_file_test.cpp

    Exercises os_file.h in a sandbox folder next to the test executable,
    which is deleted at the start and the end. Covers whole-file read /
    write / append (including atomic replace), Unicode and long paths,
    failures and their reasons and warnings, handles, info, directory
    listing, create / delete / rename, special directories and the path
    helpers. Prints FAIL lines and returns nonzero if anything failed. Build
    with `fbuild os-file-test-debug` (or -release / -profile) from the repo
    root.

    Includes base.cpp, so the log layer's file failure hook is installed;
    the test wraps it to count reports and still sees the warnings.

    NOTE: Uses printf until the os layer has its own output functions.
*/

#include <stdio.h>

#include "../../base.cpp"

static int test_failures = 0;

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                \
            ++test_failures;                                                   \
        }                                                                      \
    } while (0)

// Wraps the log layer's hook: counts reports, keeps the last one, forwards.
static OsFileFailureHookFunction *log_file_hook = 0;
static int file_failures = 0;
static const char *file_last_operation = 0;
static u8 file_last_path_storage[1024];
static String8 file_last_path = {};

static void TestFileFailure(const char *operation, String8 path, u32 os_error)
{
    ++file_failures;
    file_last_operation = operation;
    u64 size = path.size;
    if (size > sizeof(file_last_path_storage))
    {
        size = sizeof(file_last_path_storage);
    }
    MemCopy(file_last_path_storage, path.data, size);
    file_last_path = Str8Make(file_last_path_storage, size);
    if (log_file_hook != 0)
    {
        log_file_hook(operation, path, os_error);
    }
}

static b32 TestHas(String8 haystack, String8 needle)
{
    return Str8Find(haystack, needle, 0) < haystack.size;
}

static b32 TestOperationIs(const char *expected)
{
    return file_last_operation != 0 &&
           Str8Equal(Str8FromCStr(file_last_operation), Str8FromCStr(expected));
}

// Deletes path and everything under it. Quiet if it does not exist.
static void TestDeleteTree(Arena *arena, String8 path)
{
    OsFileInfo info = {};
    if (!OsFileGetInfo(path, &info))
    {
        return;
    }
    if (!(info.flags & OsFileInfo_Directory))
    {
        OsFileDelete(path, 0);
        return;
    }
    ArenaTemp temp = ArenaTempBegin(arena);
    Slice<OsDirectoryEntry> entries = OsDirectoryList(arena, path, 0);
    for (OsDirectoryEntry &entry : entries)
    {
        TestDeleteTree(arena, OsPathJoin(arena, path, entry.name));
    }
    ArenaTempEnd(temp);
    OsDirectoryDelete(path, 0);
}

static b32 TestListHas(Slice<OsDirectoryEntry> entries,
                       String8 name,
                       OsFileInfo *info)
{
    for (OsDirectoryEntry &entry : entries)
    {
        if (Str8Equal(entry.name, name))
        {
            if (info != 0)
            {
                *info = entry.info;
            }
            return 1;
        }
    }
    return 0;
}

static u64 TestNowNs(void)
{
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    u64 ticks = ((u64)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return (ticks - 116444736000000000ull) * 100;
}

static void TestPaths(Arena *arena)
{
    String8 sep = STR8_LIT("\\");
    CHECK(Str8Equal(
      OsPathJoin(arena, STR8_LIT("a"), STR8_LIT("b")),
      Str8Cat(arena, Str8Cat(arena, STR8_LIT("a"), sep), STR8_LIT("b"))));
    CHECK(Str8Equal(OsPathJoin(arena, STR8_LIT("a/"), STR8_LIT("/b")),
                    STR8_LIT("a\\b")));
    CHECK(Str8Equal(OsPathJoin(arena, STR8_LIT("a\\\\"), STR8_LIT("b/c")),
                    STR8_LIT("a\\b/c")));
    CHECK(
      Str8Equal(OsPathJoin(arena, STR8_LIT(""), STR8_LIT("b")), STR8_LIT("b")));
    CHECK(
      Str8Equal(OsPathJoin(arena, STR8_LIT("a"), STR8_LIT("")), STR8_LIT("a")));

    CHECK(Str8Equal(OsPathFileName(STR8_LIT("dir/a.tar.gz")),
                    STR8_LIT("a.tar.gz")));
    CHECK(
      Str8Equal(OsPathFileName(STR8_LIT("C:\\x\\y.png")), STR8_LIT("y.png")));
    CHECK(Str8Equal(OsPathFileName(STR8_LIT("plain")), STR8_LIT("plain")));
    CHECK(OsPathFileName(STR8_LIT("dir/")).size == 0);

    CHECK(Str8Equal(OsPathExtension(STR8_LIT("dir/a.tar.gz")), STR8_LIT("gz")));
    CHECK(OsPathExtension(STR8_LIT("noext")).size == 0);
    CHECK(OsPathExtension(STR8_LIT(".gitignore")).size == 0);
    CHECK(OsPathExtension(STR8_LIT("dir.v2/file")).size == 0);
    CHECK(OsPathExtension(STR8_LIT("a.")).size == 0);

    CHECK(
      Str8Equal(OsPathDirectory(STR8_LIT("dir/a.tar.gz")), STR8_LIT("dir")));
    CHECK(
      Str8Equal(OsPathDirectory(STR8_LIT("C:\\x\\y.png")), STR8_LIT("C:\\x")));
    CHECK(Str8Equal(OsPathDirectory(STR8_LIT("a/b/")), STR8_LIT("a/b")));
    CHECK(OsPathDirectory(STR8_LIT("plain")).size == 0);
}

static void TestWholeFiles(Arena *arena, String8 sandbox)
{
    String8 path = OsPathJoin(arena, sandbox, STR8_LIT("hello.txt"));
    OsFileError error = OsFileError_Other;
    CHECK(OsFileWrite(path, STR8_LIT("hello world"), &error));
    CHECK(error == OsFileError_None);
    String8 read = OsFileRead(arena, path, &error);
    CHECK(Str8Equal(read, STR8_LIT("hello world")));
    CHECK(read.data[read.size] == 0);
    CHECK(error == OsFileError_None);

    // Overwrite replaces, and the temp file is gone afterwards.
    CHECK(OsFileWrite(path, STR8_LIT("bye"), 0));
    CHECK(Str8Equal(OsFileRead(arena, path, 0), STR8_LIT("bye")));
    Slice<OsDirectoryEntry> entries = OsDirectoryList(arena, sandbox, 0);
    CHECK(entries.count == 1);
    CHECK(TestListHas(entries, STR8_LIT("hello.txt"), 0));

    // Empty files read as an empty but valid string.
    String8 empty_path = OsPathJoin(arena, sandbox, STR8_LIT("empty.bin"));
    String8 nothing = {};
    CHECK(OsFileWrite(empty_path, nothing, 0));
    String8 empty = OsFileRead(arena, empty_path, &error);
    CHECK(empty.size == 0 && empty.data != 0 && error == OsFileError_None);

    // Append creates, then extends.
    String8 log_path = OsPathJoin(arena, sandbox, STR8_LIT("log.txt"));
    CHECK(OsFileAppend(log_path, STR8_LIT("one\n"), 0));
    CHECK(OsFileAppend(log_path, STR8_LIT("two\n"), 0));
    CHECK(Str8Equal(OsFileRead(arena, log_path, 0), STR8_LIT("one\ntwo\n")));

    // Binary data with zeros survives.
    u8 binary[256];
    for (u32 i = 0; i < 256; ++i)
    {
        binary[i] = (u8)i;
    }
    String8 binary_path = OsPathJoin(arena, sandbox, STR8_LIT("bytes.bin"));
    CHECK(OsFileWrite(binary_path, Str8Make(binary, 256), 0));
    String8 binary_read = OsFileRead(arena, binary_path, 0);
    CHECK(binary_read.size == 256 && MemEqual(binary_read.data, binary, 256));

    // Larger than the arena's commit chunk and the scratch conversions.
    u64 big_size = 3 * 1024 * 1024 + 17;
    Slice<u8> big = SlicePushNoZero<u8>(arena, big_size);
    for (u64 i = 0; i < big.count; ++i)
    {
        big.data[i] = (u8)(i * 31);
    }
    String8 big_path = OsPathJoin(arena, sandbox, STR8_LIT("big.bin"));
    CHECK(OsFileWrite(big_path, Str8Make(big.data, big.count), 0));
    String8 big_read = OsFileRead(arena, big_path, 0);
    CHECK(big_read.size == big_size &&
          MemEqual(big_read.data, big.data, big_size));

    OsFileDelete(path, 0);
    OsFileDelete(empty_path, 0);
    OsFileDelete(log_path, 0);
    OsFileDelete(binary_path, 0);
    OsFileDelete(big_path, 0);
    CHECK(OsDirectoryList(arena, sandbox, 0).count == 0);
}

static void TestUnicodeAndLongPaths(Arena *arena, String8 sandbox)
{
    String8 name = STR8_LIT("caf\xC3\xA9 \xE2\x82\xAC.txt");
    String8 path = OsPathJoin(arena, sandbox, name);
    CHECK(OsFileWrite(path, STR8_LIT("unicode"), 0));
    CHECK(Str8Equal(OsFileRead(arena, path, 0), STR8_LIT("unicode")));
    Slice<OsDirectoryEntry> entries = OsDirectoryList(arena, sandbox, 0);
    CHECK(TestListHas(entries, name, 0));
    CHECK(OsFileDelete(path, 0));

    // Six nested 50-character folders: well past MAX_PATH (260).
    String8 deep = sandbox;
    String8 top = {};
    for (int level = 0; level < 6; ++level)
    {
        deep =
          OsPathJoin(arena, deep, Str8PushF(arena, "level%d_%044d", level, 0));
        if (level == 0)
        {
            top = deep;
        }
    }
    String8 deep_file = OsPathJoin(arena, deep, STR8_LIT("deep.txt"));
    CHECK(deep_file.size > 300);
    CHECK(OsDirectoryCreate(deep, 0));
    CHECK(OsFileExists(deep));
    CHECK(OsFileWrite(deep_file, STR8_LIT("deep"), 0));
    CHECK(Str8Equal(OsFileRead(arena, deep_file, 0), STR8_LIT("deep")));
    OsFileInfo info = {};
    CHECK(OsFileGetInfo(deep_file, &info) && info.size == 4);
    CHECK(OsDirectoryList(arena, deep, 0).count == 1);
    TestDeleteTree(arena, top);
    CHECK(!OsFileExists(deep_file));
    CHECK(!OsFileExists(top));
}

static void TestFailures(Arena *arena, String8 sandbox)
{
    String8 missing = OsPathJoin(arena, sandbox, STR8_LIT("missing.txt"));

    // A failed read: empty, NotFound, one report with the path, a warning.
    file_failures = 0;
    OsFileError error = OsFileError_None;
    LogScopeBegin(arena);
    String8 read = OsFileRead(arena, missing, &error);
    String8List warnings = LogScopeEnd();
    CHECK(read.size == 0 && read.data == 0);
    CHECK(error == OsFileError_NotFound);
    CHECK(file_failures == 1 && TestOperationIs("read"));
    CHECK(Str8Equal(file_last_path, missing));
    if constexpr (LogLevel_Warning >= LOG_MIN_LEVEL)
    {
        CHECK(warnings.count == 1);
        if (warnings.count == 1)
        {
            String8 line = warnings.first->string;
            CHECK(TestHas(line, STR8_LIT("warning: OsFile read \"")));
            CHECK(TestHas(line, missing));
            CHECK(
              Str8EndsWith(line, OsErrorMessage(arena, ERROR_FILE_NOT_FOUND)));
        }
    }

    // Queries treat "not found" as an answer, not a failure.
    file_failures = 0;
    OsFileInfo info = {};
    CHECK(!OsFileExists(missing));
    CHECK(!OsFileGetInfo(missing, &info));
    CHECK(!OsFileExists(OsPathJoin(arena, missing, STR8_LIT("deeper"))));
    CHECK(file_failures == 0);

    // A missing parent directory is NotFound too.
    String8 orphan = OsPathJoin(
      arena, OsPathJoin(arena, sandbox, STR8_LIT("nope")), STR8_LIT("x.txt"));
    CHECK(!OsFileWrite(orphan, STR8_LIT("x"), &error));
    CHECK(error == OsFileError_NotFound);
    CHECK(OsDirectoryList(arena, OsPathDirectory(orphan), &error).count == 0);
    CHECK(error == OsFileError_NotFound && TestOperationIs("list"));

    // A file held without sharing is InUse.
    String8 locked = OsPathJoin(arena, sandbox, STR8_LIT("locked.txt"));
    CHECK(OsFileWrite(locked, STR8_LIT("locked"), 0));
    String16 locked16 = Str16From8(arena, locked);
    HANDLE exclusive = CreateFileW((wchar_t *)locked16.data,
                                   GENERIC_READ,
                                   0,
                                   0,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   0);
    CHECK(exclusive != INVALID_HANDLE_VALUE);
    CHECK(OsFileRead(arena, locked, &error).size == 0);
    CHECK(error == OsFileError_InUse);
    CloseHandle(exclusive);
    CHECK(Str8Equal(OsFileRead(arena, locked, &error), STR8_LIT("locked")));
    CHECK(error == OsFileError_None);
    OsFileDelete(locked, 0);

    // Deleting a non-empty directory is NotEmpty; a file as a directory
    // fails too.
    String8 full = OsPathJoin(arena, sandbox, STR8_LIT("full"));
    CHECK(OsDirectoryCreate(full, 0));
    CHECK(
      OsFileWrite(OsPathJoin(arena, full, STR8_LIT("f")), STR8_LIT("f"), 0));
    CHECK(!OsDirectoryDelete(full, &error));
    CHECK(error == OsFileError_NotEmpty && TestOperationIs("delete directory"));
    CHECK(!OsDirectoryCreate(OsPathJoin(arena, full, STR8_LIT("f")), &error));
    CHECK(error == OsFileError_AlreadyExists);
    TestDeleteTree(arena, full);

    CHECK(!OsFileDelete(missing, &error) && error == OsFileError_NotFound);
}

static void TestHandles(Arena *arena, String8 sandbox)
{
    String8 path = OsPathJoin(arena, sandbox, STR8_LIT("handle.bin"));
    OsFileError error = OsFileError_Other;
    OsFile file = OsFileOpen(
      path, OsFileOpen_Read | OsFileOpen_Write | OsFileOpen_Create, &error);
    CHECK(file.handle != 0 && error == OsFileError_None);
    CHECK(OsFileSize(file) == 0);
    CHECK(OsFileWriteAt(file, 100, STR8_LIT("abcdef")) == 6);
    CHECK(OsFileSize(file) == 106);

    u8 buffer[16] = {};
    CHECK(OsFileReadAt(file, 100, buffer, 6) == 6);
    CHECK(MemEqual(buffer, "abcdef", 6));
    CHECK(OsFileReadAt(file, 0, buffer, 4) == 4 && buffer[0] == 0);
    // Short read at the end, nothing past it; neither is a failure.
    file_failures = 0;
    CHECK(OsFileReadAt(file, 103, buffer, 16) == 3);
    CHECK(MemEqual(buffer, "def", 3));
    CHECK(OsFileReadAt(file, 1000, buffer, 16) == 0);
    CHECK(file_failures == 0);
    OsFileClose(file);

    // Read-only open sees the data; Truncate empties it.
    OsFile reader = OsFileOpen(path, OsFileOpen_Read, 0);
    CHECK(OsFileSize(reader) == 106);
    OsFileClose(reader);
    OsFile truncated =
      OsFileOpen(path, OsFileOpen_Write | OsFileOpen_Truncate, 0);
    CHECK(truncated.handle != 0 && OsFileSize(truncated) == 0);
    OsFileClose(truncated);

    // Opening a missing file without Create fails with NotFound.
    OsFile missing =
      OsFileOpen(OsPathJoin(arena, sandbox, STR8_LIT("absent.bin")),
                 OsFileOpen_Read,
                 &error);
    CHECK(missing.handle == 0 && error == OsFileError_NotFound);
    CHECK(TestOperationIs("open"));

    // The nil file: everything is a quiet no-op.
    OsFile nil = {};
    file_failures = 0;
    CHECK(OsFileSize(nil) == 0);
    CHECK(OsFileReadAt(nil, 0, buffer, 16) == 0);
    CHECK(OsFileWriteAt(nil, 0, STR8_LIT("x")) == 0);
    OsFileClose(nil);
    CHECK(file_failures == 0);

    OsFileDelete(path, 0);
}

static void TestDirectories(Arena *arena, String8 sandbox)
{
    String8 a = OsPathJoin(arena, sandbox, STR8_LIT("a.txt"));
    String8 b = OsPathJoin(arena, sandbox, STR8_LIT("b.bin"));
    String8 sub = OsPathJoin(arena, sandbox, STR8_LIT("sub"));
    u64 before_ns = TestNowNs();
    CHECK(OsFileWrite(a, STR8_LIT("aaa"), 0));
    CHECK(OsFileWrite(b, STR8_LIT("bbbbb"), 0));
    CHECK(OsDirectoryCreate(sub, 0));
    CHECK(OsDirectoryCreate(sub, 0)); // existing directory: success

    Slice<OsDirectoryEntry> entries = OsDirectoryList(arena, sandbox, 0);
    CHECK(entries.count == 3);
    OsFileInfo info = {};
    CHECK(TestListHas(entries, STR8_LIT("a.txt"), &info));
    CHECK(info.size == 3 && !(info.flags & OsFileInfo_Directory));
    CHECK(TestListHas(entries, STR8_LIT("b.bin"), &info) && info.size == 5);
    CHECK(TestListHas(entries, STR8_LIT("sub"), &info));
    CHECK(info.flags & OsFileInfo_Directory);
    CHECK(OsDirectoryList(arena, sub, 0).count == 0);

    // Info: sizes, flags, a plausible modification time.
    CHECK(OsFileGetInfo(a, &info));
    CHECK(info.size == 3);
    u64 year_2020_ns = 1577836800ull * 1000000000ull;
    CHECK(info.modified_ns > year_2020_ns);
    CHECK(info.modified_ns + 2000000000ull >= before_ns);
    CHECK(info.modified_ns <= TestNowNs() + 2000000000ull);
    CHECK(OsFileGetInfo(sub, &info) && (info.flags & OsFileInfo_Directory));

    // Nested create makes the parents.
    String8 nested = OsPathJoin(arena, sub, STR8_LIT("x/y/z"));
    CHECK(OsDirectoryCreate(nested, 0));
    CHECK(OsFileExists(nested));
    CHECK(OsDirectoryCreate(Str8Cat(arena, nested, STR8_LIT("/")), 0));

    // Rename replaces the target; delete removes.
    CHECK(OsFileRename(a, b, 0));
    CHECK(!OsFileExists(a));
    CHECK(Str8Equal(OsFileRead(arena, b, 0), STR8_LIT("aaa")));
    OsFileError error = OsFileError_None;
    CHECK(!OsFileRename(a, b, &error) && error == OsFileError_NotFound);
    CHECK(TestOperationIs("rename") && TestHas(file_last_path, STR8_LIT("->")));
    CHECK(OsFileDelete(b, 0));
    CHECK(!OsFileExists(b));
    TestDeleteTree(arena, sub);
    CHECK(OsDirectoryList(arena, sandbox, 0).count == 0);
}

int main(void)
{
    Arena *arena = ArenaAllocDefault();
    LogSetConsoleOutput(0);
    log_file_hook = os_file_failure_hook;
    os_file_failure_hook = TestFileFailure;

    String8 exe_directory = OsExecutableDirectory(arena);
    CHECK(Str8EndsWith(exe_directory, STR8_LIT("os_file_test")));
    CHECK(exe_directory.data[exe_directory.size] == 0);
    CHECK(OsCurrentDirectory(arena).size > 0);

    String8 sandbox = OsPathJoin(arena, exe_directory, STR8_LIT("sandbox"));
    TestDeleteTree(arena, sandbox);
    CHECK(OsDirectoryCreate(sandbox, 0));

    TestPaths(arena);
    TestWholeFiles(arena, sandbox);
    TestUnicodeAndLongPaths(arena, sandbox);
    TestFailures(arena, sandbox);
    TestHandles(arena, sandbox);
    TestDirectories(arena, sandbox);

    TestDeleteTree(arena, sandbox);
    CHECK(!OsFileExists(sandbox));
    ArenaRelease(arena);

    if (test_failures == 0)
    {
        printf("os file: all tests passed\n");
    }
    return test_failures;
}
