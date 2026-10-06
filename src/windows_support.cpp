#include "windows_support.h"
#include "test_hooks.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace snip
{
namespace
{
class RegistryKey
{
  public:
    HKEY value = nullptr;
    ~RegistryKey()
    {
        if (value)
            RegCloseKey(value);
    }
};
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
} // namespace
std::string windowsError(const char *operation, DWORD code)
{
    wchar_t description[512]{};
    const DWORD count =
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                       description, static_cast<DWORD>(std::size(description)), nullptr);
    char utf8[2048]{};
    if (count)
        WideCharToMultiByte(CP_UTF8, 0, description, -1, utf8, static_cast<int>(std::size(utf8)),
                            nullptr, nullptr);
    char number[48]{};
    std::snprintf(number, sizeof(number), " (Windows error 0x%08lX)",
                  static_cast<unsigned long>(code));
    std::string result = std::string(operation) + number;
    if (utf8[0])
        result += ": " + std::string(utf8);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
        result.pop_back();
    return result;
}
[[noreturn]] void throwWindowsError(const char *operation, DWORD code)
{
    throw std::runtime_error(windowsError(operation, code ? code : ERROR_GEN_FAILURE));
}
void check(HRESULT result, const char *operation)
{
    if (FAILED(result))
        throwWindowsError(operation, static_cast<DWORD>(result));
}
void showError(HWND owner, const char *text) noexcept
{
#ifdef TIGER_SNIP_TESTING
    if (testing::errorSink)
    {
        testing::errorSink(text);
        return;
    }
#endif
    // Error reporting must still work after allocation failure, and must not recurse
    // if painting the message box triggers another failed editor paint.
    static thread_local bool showing = false;
    if (showing)
    {
        OutputDebugStringA(text);
        return;
    }
    showing = true;
    wchar_t message[2048]{};
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, message, static_cast<int>(std::size(message))))
        wcscpy_s(message, L"Tiger Snip encountered an error. Close the app and retry.");
    MessageBoxW(owner, message, L"Tiger Snip", MB_OK | MB_ICONERROR);
    showing = false;
}
std::wstring executablePath()
{
    for (DWORD capacity = 512; capacity <= 32768; capacity = std::min(capacity * 2, 32768UL))
    {
        std::vector<wchar_t> path(capacity);
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), capacity);
        if (!length)
            throwWindowsError("Cannot locate the running application.");
        if (length < capacity)
            return {path.data(), length};
        if (capacity == 32768)
            break;
    }
    throwWindowsError("The application path is too long.", ERROR_FILENAME_EXCED_RANGE);
}
std::optional<std::wstring> readRegistryString(HKEY root, const wchar_t *path, const wchar_t *name)
{
    RegistryKey key;
    LONG result = RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key.value);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        return {};
    if (result != ERROR_SUCCESS)
        throwWindowsError("Cannot read the startup setting.", result);
    DWORD bytes = 0, type = 0;
    result = RegQueryValueExW(key.value, name, nullptr, &type, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND)
        return {};
    if (result != ERROR_SUCCESS)
        throwWindowsError("Cannot inspect the startup setting.", result);
    if (type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 32768 * sizeof(wchar_t) ||
        bytes % sizeof(wchar_t))
        return {};
    std::vector<wchar_t> value(bytes / sizeof(wchar_t));
    result = RegQueryValueExW(key.value, name, nullptr, &type,
                              reinterpret_cast<BYTE *>(value.data()), &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_MORE_DATA)
        return {};
    if (result != ERROR_SUCCESS)
        throwWindowsError("Cannot read the startup setting.", result);
    if (type != REG_SZ || !bytes || bytes % sizeof(wchar_t) ||
        bytes > value.size() * sizeof(wchar_t))
        return {};
    return boundedRegistryString(type, value.data(), bytes);
}
std::optional<std::wstring> boundedRegistryString(DWORD type, const wchar_t *value, size_t bytes)
{
    if (type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 32768 * sizeof(wchar_t) ||
        bytes % sizeof(wchar_t))
        return {};
    const size_t length = bytes / sizeof(wchar_t);
    if (value[length - 1] != L'\0' ||
        std::find(value, value + length - 1, L'\0') != value + length - 1)
        return {};
    return std::wstring(value, length - 1);
}
bool startupEnabled(const std::wstring &executable)
{
    const auto value = readRegistryString(HKEY_CURRENT_USER, RunKey, L"TigerSnip");
    return value && *value == L"\"" + executable + L"\" --tray";
}
void setStartupEnabled(const std::wstring &executable, bool enabled)
{
    const auto command = L"\"" + executable + L"\" --tray";
    RegistryKey key;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                                  &key.value, nullptr);
    if (result != ERROR_SUCCESS)
        throwWindowsError("Cannot change the startup setting.", result);
    result = enabled ? RegSetValueExW(key.value, L"TigerSnip", 0, REG_SZ,
                                      reinterpret_cast<const BYTE *>(command.c_str()),
                                      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)))
                     : RegDeleteValueW(key.value, L"TigerSnip");
    if (result != ERROR_SUCCESS && !(result == ERROR_FILE_NOT_FOUND && !enabled))
        throwWindowsError("Cannot update the startup setting.", result);
}
bool messageAvailable(int result, DWORD failure)
{
    if (result == -1)
        throwWindowsError("Windows could not read the next application event.", failure);
    return result != 0;
}
void forwardExistingLaunch(const wchar_t *windowClass, UINT message, WPARAM request,
                           DWORD timeoutMs)
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;)
    {
        // The instance mutex is created before the window. A simultaneous launch
        // waits briefly for that window instead of silently discarding its request.
        if (HWND existing = FindWindowW(windowClass, nullptr))
        {
            DWORD pid = 0;
            if (GetWindowThreadProcessId(existing, &pid))
            {
                AllowSetForegroundWindow(pid); // Foreground permission is best effort.
                if (PostMessageW(existing, message, request, 0))
                    return;
                const DWORD failure = GetLastError();
                if (failure != ERROR_INVALID_WINDOW_HANDLE)
                    throwWindowsError("Windows could not open the running Tiger Snip window.",
                                      failure);
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            throwWindowsError(
                "Tiger Snip is still starting or could not start. Wait a moment and try again.",
                ERROR_TIMEOUT);
        Sleep(static_cast<DWORD>(std::min<ULONGLONG>(20, deadline - now)));
    }
}
} // namespace snip
