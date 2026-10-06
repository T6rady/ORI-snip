#include "file_io.h"
#include "windows_support.h"
#include "test_hooks.h"
#include <objbase.h>

#include <stdexcept>

namespace snip
{
namespace
{
struct SaveStagingFile
{
    const std::wstring &path;
    HANDLE handle;
    ~SaveStagingFile()
    {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
        // Only this save's newly created staging file is eligible for cleanup.
        // Recovery copies and leftovers from interrupted saves are never swept.
        DeleteFileW(path.c_str());
    }
};
} // namespace
void saveBytes(const std::wstring &path, const std::vector<uint8_t> &bytes)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    const bool replacing = attributes != INVALID_FILE_ATTRIBUTES;
    if (!replacing && GetLastError() != ERROR_FILE_NOT_FOUND)
        throwWindowsError("Cannot access this location. Choose another folder.");
    if (replacing && (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)))
        throwWindowsError("This location is read-only or is a folder. Choose another filename.",
                          ERROR_ACCESS_DENIED);

    // Give the temporary image the existing file's permissions before writing any pixels.
    // ReplaceFile also preserves the destination's DACL and other file attributes on overwrite.
    std::vector<uint8_t> security;
    SECURITY_ATTRIBUTES access{sizeof(access), nullptr, FALSE};
    if (replacing)
    {
        DWORD needed = 0;
        GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
        if (!needed || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            throwWindowsError("Cannot read this file's permissions. Choose another filename.");
        security.resize(needed);
        if (!GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, security.data(), needed,
                              &needed))
            throwWindowsError("Cannot read this file's permissions. Choose another filename.");
        access.lpSecurityDescriptor = security.data();
    }
    GUID unique{};
    check(CoCreateGuid(&unique), "Cannot identify this save attempt.");
    wchar_t id[40]{};
    if (!StringFromGUID2(unique, id, static_cast<int>(std::size(id))))
        throwWindowsError("Cannot identify this save attempt.", ERROR_GEN_FAILURE);
    const std::wstring temporary = path + L".tiger-snip-" + id + L".tmp";
    const std::wstring backup = temporary + L".previous";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, replacing ? &access : nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throwWindowsError("Cannot write this location. Choose another folder.");
    SaveStagingFile staging{temporary, file};
    DWORD written = 0;
    bool success =
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(file);
    const DWORD writeFailure = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    staging.handle = INVALID_HANDLE_VALUE;
    if (!success)
    {
        throwWindowsError("Could not write the file. The original file was preserved.",
                          writeFailure);
    }
#ifdef TIGER_SNIP_TESTING
    if (testing::fileSaveCheckpoint)
        testing::fileSaveCheckpoint(temporary.c_str(), backup.c_str());
#endif
    if (!replacing)
    {
        // Never overwrite a file that appeared after the initial existence check.
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
            return;
        const DWORD failure = GetLastError();
        throwWindowsError("Could not finish saving the file. Choose another filename or retry.",
                          failure);
    }

    const DWORD backupAttributes = GetFileAttributesW(backup.c_str());
    if (backupAttributes != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND)
    {
        throw std::runtime_error("An earlier save recovery file exists. Choose another filename.");
    }
    if (ReplaceFileW(path.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr))
    {
        DeleteFileW(backup.c_str());
        return;
    }
    const DWORD replaceFailure = GetLastError();

    // Some ReplaceFile failures can leave the original at the backup name. Restore it
    // without overwriting another file; retain the recovery copy if restoration fails.
    if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        if (!MoveFileExW(backup.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            throwWindowsError("Could not finish saving. The original is retained in the "
                              "adjacent .tmp.previous recovery file; choose another filename.",
                              replaceFailure);
        }
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        throwWindowsError("Could not finish saving the file. The original file was preserved.",
                          replaceFailure);
    throwWindowsError("Could not finish saving the file. Check the destination folder.",
                      replaceFailure);
}
} // namespace snip
