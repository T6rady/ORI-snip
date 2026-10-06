#include "graphics.h"
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
std::vector<uint8_t> readBytes(const std::wstring &path)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    require(file.is_open(), "Cannot read saved fixture.");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::wstring currentUserSid()
{
    HANDLE token = nullptr;
    require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token), "Cannot read user token.");
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    std::vector<uint8_t> data(needed);
    const bool read = GetTokenInformation(token, TokenUser, data.data(), needed, &needed) != FALSE;
    CloseHandle(token);
    require(read, "Cannot read user SID.");
    LPWSTR text = nullptr;
    require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &text),
            "Cannot convert user SID.");
    std::wstring result = text;
    LocalFree(text);
    return result;
}
void restrictToCurrentUser(const std::wstring &path)
{
    const auto sddl = L"D:P(A;;FA;;;" + currentUserSid() + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr), "Cannot create private DACL.");
    const bool set = SetFileSecurityW(path.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor) != FALSE;
    LocalFree(descriptor);
    require(set, "Cannot apply private fixture permissions.");
}
struct Permissions
{
    bool protectedDacl;
    std::vector<uint8_t> dacl;
    bool operator==(const Permissions &) const = default;
};
Permissions permissions(const std::wstring &path)
{
    DWORD needed = 0;
    GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
    require(needed > 0, "Cannot size fixture permissions.");
    std::vector<uint8_t> descriptor(needed);
    require(GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION,
                            descriptor.data(), needed, &needed), "Cannot read fixture permissions.");
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    require(GetSecurityDescriptorControl(descriptor.data(), &control, &revision),
            "Cannot read DACL protection.");
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    require(GetSecurityDescriptorDacl(descriptor.data(), &present, &dacl, &defaulted) && present && dacl,
            "Fixture has no explicit DACL.");
    const auto first = reinterpret_cast<const uint8_t *>(dacl);
    // Windows may update bookkeeping flags such as AUTO_INHERITED during replacement.
    // Compare the actual access rules and protection from future folder inheritance.
    return {(control & SE_DACL_PROTECTED) != 0, {first, first + dacl->AclSize}};
}
void expectFailure(const std::wstring &path, const std::vector<uint8_t> &bytes)
{
    bool failed = false;
    try
    {
        snip::saveBytes(path, bytes);
    }
    catch (const std::runtime_error &)
    {
        failed = true;
    }
    require(failed, "Save unexpectedly succeeded.");
}
}

int wmain()
{
    const auto directory = (std::filesystem::current_path() /
        (L"file-save-test-" + std::to_wstring(GetCurrentProcessId()))).wstring();
    if (!CreateDirectoryW(directory.c_str(), nullptr))
    {
        std::cout << "FAIL: cannot create unique test directory.\n";
        return 1;
    }
    const auto path = (std::filesystem::path(directory) / L"private screenshot.png").wstring();
    const auto temporary = path + L".tiger-snip-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    const auto backup = temporary + L".previous";
    HANDLE held = INVALID_HANDLE_VALUE;
    int result = 0;
    try
    {
        const std::vector<uint8_t> original{1, 2, 3, 4}, replacement{10, 20, 30};
        snip::saveBytes(path, original);
        require(readBytes(path) == original, "New-file contents differ.");
        restrictToCurrentUser(path);
        const auto before = permissions(path);
        require(before.protectedDacl, "Fixture DACL is not protected.");
        snip::saveBytes(path, replacement);
        require(readBytes(path) == replacement && permissions(path) == before,
                "Overwrite changed contents incorrectly or broadened file permissions.");
        snip::saveBytes(path, original);
        require(readBytes(path) == original && permissions(path) == before,
                "Repeated overwrite lost private permissions.");
        require(!std::filesystem::exists(temporary) && !std::filesystem::exists(backup),
                "Successful save left temporary/recovery files.");

        held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "Cannot lock fixture against replacement.");
        expectFailure(path, replacement);
        CloseHandle(held);
        held = INVALID_HANDLE_VALUE;
        require(readBytes(path) == original && permissions(path) == before,
                "Locked-file failure changed the original or its permissions.");
        require(!std::filesystem::exists(temporary) && !std::filesystem::exists(backup),
                "Locked-file failure left an unnecessary temporary/recovery file.");

        require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), "Cannot set read-only fixture.");
        expectFailure(path, replacement);
        require(readBytes(path) == original && permissions(path) == before &&
                (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_READONLY),
                "Read-only failure changed the original.");
        require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL), "Cannot restore fixture attributes.");

        // A previous recovery file must never be overwritten by a new save attempt.
        snip::saveBytes(backup, replacement);
        expectFailure(path, replacement);
        require(readBytes(path) == original && readBytes(backup) == replacement &&
                permissions(path) == before, "Existing recovery data was overwritten.");
        require(DeleteFileW(backup.c_str()), "Cannot remove recovery fixture.");
        expectFailure(directory, replacement);
        expectFailure((std::filesystem::path(directory) / L"missing" / L"capture.png").wstring(), replacement);
        std::cout << "PASS: new and repeated saves, protected per-user DACL preservation, locked/read-only "
                     "failure preservation, temporary cleanup, recovery-file protection, invalid destinations.\n";
    }
    catch (const std::exception &exception)
    {
        std::cout << "FAIL: " << exception.what() << " Windows error=" << GetLastError() << '\n';
        result = 1;
    }
    if (held != INVALID_HANDLE_VALUE)
        CloseHandle(held);
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path.c_str());
    DeleteFileW(temporary.c_str());
    DeleteFileW(backup.c_str());
    RemoveDirectoryW(directory.c_str());
    return result;
}
