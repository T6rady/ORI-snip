#include "windows_support.h"
#include <objbase.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr UINT Launch = WM_APP + 21;
unsigned opened = 0, snipped = 0;
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == Launch)
    {
        if (wp)
            ++snipped;
        else
            ++opened;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
} // namespace
int wmain(int argc, wchar_t **argv)
{
    if (argc == 5 && wcscmp(argv[1], L"--send") == 0)
    {
        HANDLE ready = OpenSemaphoreW(SEMAPHORE_MODIFY_STATE, FALSE, argv[4]);
        if (!ready || !ReleaseSemaphore(ready, 1, nullptr))
            return 2;
        CloseHandle(ready);
        try
        {
            snip::forwardExistingLaunch(argv[2], Launch, wcscmp(argv[3], L"1") == 0, 3000);
            return 0;
        }
        catch (...)
        {
            return 3;
        }
    }
    const auto originalStation = GetProcessWindowStation();
    const auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    HANDLE mutex = nullptr, ready = nullptr;
    HWND window = nullptr;
    std::array<HANDLE, 3> children{};
    int result = 0;
    std::wstring className;
    try
    {
        GUID unique{};
        snip::check(CoCreateGuid(&unique), "Cannot identify launch test.");
        wchar_t id[40]{};
        StringFromGUID2(unique, id, static_cast<int>(std::size(id)));
        const std::wstring desktopName = L"LaunchTest";
        className = L"TigerSnip.LaunchTest." + std::wstring(id);
        const std::wstring readyName = L"Local\\TigerSnip.ReadyTest." + std::wstring(id);
        const std::wstring mutexName = L"Local\\TigerSnip.InstanceTest." + std::wstring(id);
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        require(station && SetProcessWindowStation(station), "Cannot isolate test window station.");
        wchar_t stationName[256]{};
        require(
            GetUserObjectInformationW(station, UOI_NAME, stationName, sizeof(stationName), nullptr),
            "Cannot locate private test window station.");
        const std::wstring desktopPath = std::wstring(stationName) + L"\\" + desktopName;
        desktop = CreateDesktopW(desktopName.c_str(), nullptr, nullptr, 0,
                                 DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS |
                                     DESKTOP_WRITEOBJECTS,
                                 nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot isolate test desktop.");
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = className.c_str();
        require(RegisterClassW(&cls), "Cannot register test window class.");
        mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        ready = CreateSemaphoreW(nullptr, 0, 3, readyName.c_str());
        require(mutex && ready, "Cannot create launch synchronization fixtures.");
        // The first instance has its mutex but no window yet, as in real startup.
        // Three simultaneous processes must retain both Open and Snip requests.
        for (size_t i = 0; i < children.size(); ++i)
        {
            std::wstring command = L"\"" + snip::executablePath() + L"\" --send \"" + className +
                                   L"\" " + (i == 2 ? L"1" : L"0") + L" \"" + readyName + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.lpDesktop = const_cast<wchar_t *>(desktopPath.c_str());
            PROCESS_INFORMATION child{};
            require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child),
                    "Cannot start simultaneous launch.");
            CloseHandle(child.hThread);
            children[i] = child.hProcess;
        }
        for (size_t i = 0; i < children.size(); ++i)
            require(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0,
                    "Second launch did not start.");
        Sleep(150); // Deliberately expose the mutex-before-window gap.
        window = CreateWindowExW(0, className.c_str(), L"Launch fixture", WS_OVERLAPPEDWINDOW, 0, 0,
                                 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
        require(window != nullptr, "Cannot create delayed first window.");
        require(WaitForMultipleObjects(static_cast<DWORD>(children.size()), children.data(), TRUE,
                                       5000) == WAIT_OBJECT_0,
                "Second launches did not finish after the first window appeared.");
        for (HANDLE child : children)
        {
            DWORD exit = 1;
            require(GetExitCodeProcess(child, &exit) && exit == 0,
                    "Second launch failed to forward its request.");
        }
        auto pump = [&] {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                DispatchMessageW(&message);
        };
        pump();
        require(opened == 2 && snipped == 1,
                "Simultaneous Open/Snip requests were lost or duplicated.");
        snip::forwardExistingLaunch(className.c_str(), Launch, 0);
        pump();
        require(opened == 3 && snipped == 1, "A launch with an existing window failed.");
        DestroyWindow(window);
        window = nullptr;
        bool failed = false;
        const auto start = GetTickCount64();
        try
        {
            snip::forwardExistingLaunch(className.c_str(), Launch, 0, 80);
        }
        catch (const std::runtime_error &error)
        {
            failed = std::string(error.what()).find("try again") != std::string::npos;
        }
        require(failed && GetTickCount64() - start < 2000,
                "Missing first window did not produce a bounded, actionable failure.");
        std::cout << "PASS: three simultaneous processes wait for a delayed first window; "
                     "Open/Snip requests delivered once; existing-window launch and bounded "
                     "missing-window failure. User app/settings untouched.\n";
    }
    catch (const std::exception &error)
    {
        std::cout << "FAIL: " << error.what() << " Windows error=" << GetLastError() << "\n";
        result = 1;
    }
    for (HANDLE child : children)
        if (child)
        {
            if (WaitForSingleObject(child, 0) != WAIT_OBJECT_0)
                TerminateProcess(child, 4);
            CloseHandle(child);
        }
    if (window)
        DestroyWindow(window);
    if (!className.empty())
        UnregisterClassW(className.c_str(), GetModuleHandleW(nullptr));
    if (ready)
        CloseHandle(ready);
    if (mutex)
        CloseHandle(mutex);
    SetThreadDesktop(originalDesktop);
    if (desktop)
        CloseDesktop(desktop);
    SetProcessWindowStation(originalStation);
    if (station)
        CloseWindowStation(station);
    return result;
}
