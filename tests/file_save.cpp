#include "graphics.h"
#include "test_hooks.h"
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
std::wstring lastTemporary, lastBackup;
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

int wmain(int argc, wchar_t **argv)
{
    if (argc == 3 && wcscmp(argv[1], L"--interrupt-save") == 0)
    {
        snip::testing::fileSaveCheckpoint = [](const wchar_t *, const wchar_t *) {
            ExitProcess(77);
        };
        snip::saveBytes(argv[2], {10, 20, 30});
        return 2;
    }
    const auto directory = (std::filesystem::current_path() /
        (L"file-save-test-" + std::to_wstring(GetCurrentProcessId()))).wstring();
    if (!CreateDirectoryW(directory.c_str(), nullptr))
    {
        std::cout << "FAIL: cannot create unique test directory.\n";
        return 1;
    }
    const auto path = (std::filesystem::path(directory) / L"private screenshot.png").wstring();
    const auto temporary = path + L".ori-snip-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    const auto backup = temporary + L".previous";
    HANDLE held = INVALID_HANDLE_VALUE;
    int result = 0;
    try
    {
        snip::testing::fileSaveCheckpoint = [](const wchar_t *stage, const wchar_t *recovery) {
            lastTemporary = stage;
            lastBackup = recovery;
        };
        auto noCurrentStaging = [&] {
            require(!std::filesystem::exists(lastTemporary) && !std::filesystem::exists(lastBackup),
                    "Save left its own unnecessary temporary/recovery file.");
        };
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
        noCurrentStaging();

        held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "Cannot lock fixture against replacement.");
        expectFailure(path, replacement);
        CloseHandle(held);
        held = INVALID_HANDLE_VALUE;
        require(readBytes(path) == original && permissions(path) == before,
                "Locked-file failure changed the original or its permissions.");
        noCurrentStaging();

        require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), "Cannot set read-only fixture.");
        expectFailure(path, replacement);
        require(readBytes(path) == original && permissions(path) == before &&
                (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_READONLY),
                "Read-only failure changed the original.");
        require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL), "Cannot restore fixture attributes.");

        // Old PID-based leftovers neither block a later save nor get deleted.
        snip::saveBytes(temporary, original);
        snip::saveBytes(backup, replacement);
        snip::saveBytes(path, replacement);
        require(readBytes(path) == replacement && readBytes(temporary) == original &&
                    readBytes(backup) == replacement && permissions(path) == before,
                "Old temporary/recovery files blocked saving or were changed.");
        noCurrentStaging();
        snip::saveBytes(path, original);

        // Terminate a separate producer after its protected staging pixels are flushed.
        // This bypasses destructors just like a crash or forced process termination.
        std::wstring command =
            L"\"" + snip::executablePath() + L"\" --interrupt-save \"" + path + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup, &child),
                "Cannot start interrupted-save fixture.");
        CloseHandle(child.hThread);
        const auto waited = WaitForSingleObject(child.hProcess, 10000);
        if (waited != WAIT_OBJECT_0)
            TerminateProcess(child.hProcess, 2);
        DWORD exit = 0;
        const bool exited = GetExitCodeProcess(child.hProcess, &exit) != FALSE;
        CloseHandle(child.hProcess);
        require(waited == WAIT_OBJECT_0 && exited && exit == 77,
                "Interrupted-save fixture did not terminate at the checkpoint.");
        require(readBytes(path) == original && permissions(path) == before,
                "Interrupted save changed the existing PNG.");
        std::wstring interrupted;
        for (const auto &entry : std::filesystem::directory_iterator(directory))
        {
            const auto candidate = entry.path().wstring();
            if (candidate != temporary && entry.path().extension() == L".tmp")
            {
                require(interrupted.empty(), "More than one interrupted staging file found.");
                interrupted = candidate;
            }
        }
        require(!interrupted.empty() && readBytes(interrupted) == replacement &&
                    permissions(interrupted) == before,
                "Interrupted staging pixels or protected permissions differ.");
        snip::saveBytes(path, replacement);
        require(readBytes(path) == replacement && readBytes(interrupted) == replacement &&
                    permissions(path) == before,
                "Crash leftover blocked a later save or was swept.");
        noCurrentStaging();
        require(DeleteFileW(interrupted.c_str()), "Cannot remove interrupted-save fixture.");
        require(DeleteFileW(temporary.c_str()), "Cannot remove old temporary fixture.");
        require(DeleteFileW(backup.c_str()), "Cannot remove recovery fixture.");

        // Even an unexpected recovery file at this attempt's unique name is preserved.
        snip::testing::fileSaveCheckpoint = [](const wchar_t *stage, const wchar_t *recovery) {
            lastTemporary = stage;
            lastBackup = recovery;
            std::ofstream file(std::filesystem::path(recovery), std::ios::binary);
            file << "recovery";
            file.close();
            require(bool(file), "Cannot create recovery collision fixture.");
        };
        expectFailure(path, original);
        require(readBytes(path) == replacement &&
                    readBytes(lastBackup) ==
                        std::vector<uint8_t>({'r', 'e', 'c', 'o', 'v', 'e', 'r', 'y'}) &&
                    !std::filesystem::exists(lastTemporary) && permissions(path) == before,
                "Existing recovery data was overwritten or current staging was not cleaned.");
        require(DeleteFileW(lastBackup.c_str()), "Cannot remove recovery collision fixture.");
        // An exception after staging also cleans only this attempt's temporary file.
        snip::testing::fileSaveCheckpoint = [](const wchar_t *stage, const wchar_t *recovery) {
            lastTemporary = stage;
            lastBackup = recovery;
            throw std::runtime_error("Injected failure after staging.");
        };
        expectFailure(path, original);
        noCurrentStaging();
        require(readBytes(path) == replacement, "Exception cleanup changed the existing file.");
        snip::testing::fileSaveCheckpoint = nullptr;
        expectFailure(directory, replacement);
        expectFailure((std::filesystem::path(directory) / L"missing" / L"capture.png").wstring(), replacement);
        std::cout << "PASS: new and repeated saves, protected per-user DACL preservation, "
                     "locked/read-only "
                     "failure preservation, unique save names, crash-leftover retention/retry and "
                     "permissions, scoped exception cleanup, recovery-file protection, invalid "
                     "destinations.\n";
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
