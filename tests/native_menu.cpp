// The menu divider belongs to Windows' nonclient frame, which requires a visible desktop.
#include "../src/main.cpp"
#include <iostream>

int wmain()
{
    bool com = false;
    int result = 0;
    try
    {
        auto require = [](bool ok, const char *message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        app.instance = GetModuleHandleW(nullptr);
        app.smoke = true;
        app.softwareRendering = true;
        app.classicUI = true;
        app.darkTheme = true;
        app.iniPath = (std::filesystem::current_path() / L"menu-settings.ini").wstring();
        updateInterfaceColors();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        registerClasses();
        require(CreateWindowExW(0, MainClass, L"ORI Snip menu frame test", WS_OVERLAPPEDWINDOW,
                                20, 20, 1000, 700, nullptr, createMenu(), app.instance, nullptr),
                "Cannot create the native menu test window.");
        app.windowedMenu = GetMenu(app.window);
        app.graphics.initialize();
        SetWindowPos(app.window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        for (int theme : {ThemePurple, ThemeBlue, ThemeTeal})
            for (int width : {900, 1280})
            {
                command(theme);
                SetWindowPos(app.window, nullptr, 0, 0, width, 700,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                for (bool active : {false, true})
                {
                    SendMessageW(app.window, WM_NCACTIVATE, active, 0);
                    DrawMenuBar(app.window);
                    RedrawWindow(app.window, nullptr, nullptr,
                                 RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
                    DwmFlush();
                    RECT bounds{}, client{};
                    POINT origin{};
                    GetWindowRect(app.window, &bounds);
                    GetClientRect(app.window, &client);
                    ClientToScreen(app.window, &origin);
                    const int y = origin.y - bounds.top - 1;
                    const HDC dc = GetWindowDC(app.window);
                    require(dc != nullptr, "Cannot inspect the native editor frame.");
                    bool matches = true;
                    for (int offset = 20; offset < client.right - 20; offset += 31)
                        matches &= GetPixel(dc, origin.x - bounds.left + offset, y) ==
                                   RGB(30, 35, 43);
                    ReleaseDC(app.window, dc);
                    require(matches, "The native dark menu divider still uses a light frame color.");
                }
            }
        command(AppearanceLight);
        require(!darkMenuSeparator(app.window), "The menu divider fix affects light mode.");
        command(AppearanceDark);
        command(FullScreen);
        require(!darkMenuSeparator(app.window), "The menu divider fix affects fullscreen.");
        command(FullScreen);
        command(InterfaceOrange);
        require(!darkMenuSeparator(app.window), "The menu divider fix affects the menu-free layout.");
        std::cout << "PASS: native dark menu divider across presets, window sizes and activation; "
                     "light, fullscreen and menu-free layouts unaffected.\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        result = 1;
    }
    if (app.window)
        DestroyWindow(app.window);
    if (com)
        CoUninitialize();
    return result;
}
