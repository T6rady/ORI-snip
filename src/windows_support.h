#pragma once
#include <windows.h>
#include <optional>
#include <string>
#include <exception>
#include <utility>

namespace snip
{
std::string windowsError(const char *operation, DWORD code);
[[noreturn]] void throwWindowsError(const char *operation, DWORD code = GetLastError());
void check(HRESULT result, const char *operation);
void showError(HWND owner, const char *text) noexcept;
std::wstring executablePath();
std::optional<std::wstring> boundedRegistryString(DWORD type, const wchar_t *value, size_t bytes);
std::optional<std::wstring> readRegistryString(HKEY root, const wchar_t *key, const wchar_t *name);
bool startupEnabled(const std::wstring &executable);
void setStartupEnabled(const std::wstring &executable, bool enabled);
bool messageAvailable(int result, DWORD failure);
void forwardExistingLaunch(const wchar_t *windowClass, UINT message, WPARAM request,
                           DWORD timeoutMs = 5000);

// No C++ exception, including one from recovery, may cross a Windows callback.
template <class Result, class Action, class Recovery>
Result callbackBoundary(Action &&action, Recovery &&recover, Result fallback) noexcept
{
    try
    {
        return std::forward<Action>(action)();
    }
    catch (const std::exception &failure)
    {
        try
        {
            std::forward<Recovery>(recover)(failure.what());
        }
        catch (...)
        {
            showError(nullptr, "An error occurred while recovering from a Windows event.");
        }
    }
    catch (...)
    {
        try
        {
            std::forward<Recovery>(recover)("An unexpected error occurred in a Windows event.");
        }
        catch (...)
        {
            showError(nullptr, "An error occurred while recovering from a Windows event.");
        }
    }
    return fallback;
}
} // namespace snip
