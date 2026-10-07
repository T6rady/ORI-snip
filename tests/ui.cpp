// Exercise the real editor layout and pointer handlers on a private desktop.
#include "../src/main.cpp"
#include <iostream>

int wmain()
{
    const auto originalStation = GetProcessWindowStation();
    const auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    bool com = false;
    int result = 0;
    try
    {
        auto require = [](bool ok, const char *message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        require(station && SetProcessWindowStation(station), "Cannot isolate UI test station.");
        desktop = CreateDesktopW(L"TigerSnipUITest", nullptr, nullptr, 0,
                                 DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS |
                                     DESKTOP_WRITEOBJECTS,
                                 nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot isolate UI test desktop.");
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        app.instance = GetModuleHandleW(nullptr);
        app.smoke = true;
        app.softwareRendering = true;
        app.iniPath = (std::filesystem::current_path() / L"ui-settings.ini").wstring();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        registerClasses();
        require(CreateWindowExW(0, MainClass, L"UI test", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 840,
                                nullptr, createMenu(), app.instance, nullptr),
                "Cannot create UI test editor.");
        app.windowedMenu = GetMenu(app.window);
        app.menuHidden = true;
        SetMenu(app.window, nullptr);
        ShowWindow(app.window, SW_SHOWNOACTIVATE);
        // The redesigned gear must expose the complete existing menu tree, including
        // nested export choices, without taking ownership of or losing its submenus.
        const HMENU menu = app.windowedMenu;
        const std::array<int, 14> settings = {
            Settings,           AutoCopy,         RenderingSettings,   SaveLocation, Startup,
            ProfessionalBorder, ProfessionalBlur, ProfessionalRounded, SamtecLogo,   ToggleActions,
            ToggleTools,        ToggleFormatting, FullScreen,          About};
        for (int id : settings)
            require(GetMenuState(menu, id, MF_BYCOMMAND) != static_cast<UINT>(-1),
                    "An existing setting or view option is missing.");
        for (int style = 0; style < 6; ++style)
            require(GetMenuState(app.logoMenu, LogoStyleFirst + style, MF_BYCOMMAND) !=
                        static_cast<UINT>(-1),
                    "A Samtec logo setting is missing.");
        std::array<HMENU, 5> submenus{};
        for (int i = 0; i < 5; ++i)
            submenus[i] = GetSubMenu(menu, i);
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            require(SetTimer(app.window, 97, 50,
                             [](HWND window, UINT, UINT_PTR timer, DWORD) {
                                 KillTimer(window, timer);
                                 EndMenu();
                             }) != 0,
                    "Cannot drive the Settings gear popup.");
            command(AppMenu);
            require(IsMenu(menu) && GetMenuItemCount(menu) == 5,
                    "Closing the gear popup destroyed the persistent menus.");
            for (int i = 0; i < 5; ++i)
                require(IsMenu(submenus[i]) && GetSubMenu(menu, i) == submenus[i],
                        "The gear popup lost an existing submenu.");
        }
        app.graphics.initialize();
        auto fixture = Bitmap::create(1120, 720);
        for (size_t i = 0; i < fixture.pixels.size(); i += 4)
            fixture.pixels[i] = fixture.pixels[i + 1] = fixture.pixels[i + 2] =
                fixture.pixels[i + 3] = 255;
        std::vector<Annotation> page;
        auto label = [&](Point p, const wchar_t *s, float size, Color c = Ink) {
            Annotation a;
            a.kind = Tool::Text;
            a.a = p;
            a.text = s;
            a.fontSize = size;
            a.color = c;
            a.textWidth = 1000;
            app.graphics.measureText(a);
            page.push_back(a);
        };
        auto box = [&](Rect r, Color c) {
            Annotation a;
            a.kind = Tool::Rectangle;
            a.style = 3;
            a.color = c;
            a.a = {r.left, r.top};
            a.b = {r.right, r.bottom};
            a.thickness = 1;
            page.push_back(a);
        };
        label({36, 28}, L"Engineering  /  Design review", 15, Muted);
        label({36, 72}, L"Connector assembly review", 32);
        label({36, 132}, L"Check contact alignment before releasing the drawing.", 19, Muted);
        box({36, 192, 714, 636}, rgb(245, 247, 249));
        for (int i = 0; i < 12; ++i)
        {
            box({100.0f + i * 43, 277, 119.0f + i * 43, 301}, rgb(194, 159, 81));
            box({100.0f + i * 43, 462, 119.0f + i * 43, 502}, rgb(194, 159, 81));
        }
        box({86, 300, 636, 462}, rgb(43, 48, 55));
        box({103, 325, 619, 437}, rgb(65, 70, 78));
        for (int i = 0; i < 12; ++i)
        {
            box({108.0f + i * 42, 347, 124.0f + i * 42, 415}, rgb(16, 20, 26));
            box({112.0f + i * 42, 378, 120.0f + i * 42, 405}, rgb(205, 174, 99));
        }
        label({747, 202}, L"Review checklist", 23);
        label({747, 260}, L"Contact position", 18);
        label({747, 294}, L"Housing clearance", 18);
        label({747, 328}, L"Drawing dimensions", 18);
        label({747, 420}, L"Notes", 23);
        label({747, 468}, L"Confirm the highlighted", 18, Muted);
        label({747, 497}, L"contact in the next review.", 18, Muted);
        app.image = app.graphics.flatten(fixture, page);
        app.savePath = L"Connector review.png";
        app.recentSequence = 1;
        Annotation arrow;
        arrow.kind = Tool::Arrow;
        arrow.color = Accent;
        arrow.thickness = 4;
        arrow.a = {155, 594};
        arrow.b = {329, 398};
        app.document.items = {arrow};
        app.document.selected = 0;
        updateView();
        buildButtons();
        const auto originalPixels = app.image.pixels;
        const auto originalView = app.view;
        auto button = [&](int id) {
            buildButtons();
            auto b = std::find_if(app.buttons.begin(), app.buttons.end(),
                                  [&](const Button &b) { return b.command == id; });
            require(b != app.buttons.end(), "Required UI control is missing.");
            return b->rect;
        };
        auto mouse = [&](Point p) {
            return MAKELPARAM(static_cast<int>(p.x * app.dpi), static_cast<int>(p.y * app.dpi));
        };
        auto slide = [&](int id, float from, float to, bool cancel = false) {
            const auto r = button(id);
            const float cy = (r.top + r.bottom) / 2;
            mouseDown(mouse({r.left + r.width() * from, cy}));
            require(app.sliderDrag == id && GetCapture() == app.window,
                    "Slider did not capture pointer.");
            mouseMove(mouse({r.left + r.width() * to, cy}));
            if (cancel)
                processKey(VK_ESCAPE);
            else
                mouseUp(mouse({r.left + r.width() * to, cy}));
            require(!app.sliderDrag && GetCapture() != app.window && !app.document.editing(),
                    "Slider leaked capture or undo transaction.");
        };
        slide(StrokeSlider, .05f, .18f);
        const float width = app.document.items[0].thickness;
        require(width >= 18 && width <= 20, "Pixel slider did not apply arbitrary widths.");
        require(app.document.undo() && app.document.items[0].thickness == 4 &&
                    !app.document.canUndo(),
                "Slider drag was not a single undo step.");
        require(app.document.redo() && app.document.items[0].thickness == width,
                "Slider redo failed.");
        app.document.selected = 0;
        command(StrokePresetSecond);
        require(app.document.items[0].thickness == 4, "Stroke preset failed.");
        slide(StrokeSlider, .03f, .74f, true);
        require(app.document.selected == 0 && app.document.items[0].thickness == 4 &&
                    app.thickness == 4,
                "Cancel did not restore stroke and preference.");
        slide(OpacitySlider, 1, .50f);
        require(std::abs(app.document.items[0].opacity - .5f) < .02f, "Opacity slider failed.");
        const auto translucent = app.graphics.flatten(app.image, app.document.items);
        auto opaqueItems = app.document.items;
        opaqueItems[0].opacity = 1;
        require(translucent.pixels != app.graphics.flatten(app.image, opaqueItems).pixels,
                "Opacity was omitted from export.");
        auto invisibleItems = opaqueItems;
        invisibleItems[0].opacity = 0;
        require(app.graphics.flatten(app.image, invisibleItems).pixels == app.image.pixels,
                "Zero opacity changed the image.");
        require(app.graphics.decode(app.graphics.png(translucent)).pixels == translucent.pixels,
                "Opacity PNG round trip failed.");
        require(app.document.undo() && app.document.items[0].opacity == 1, "Opacity undo failed.");
        app.document.selected = 0;
        slide(OpacitySlider, .2f, .3f, true);
        require(app.document.items[0].opacity == 1, "Opacity cancellation failed.");
        command(styleCommand(Tool::Arrow, 2), true);
        require(app.document.selected == 0 && app.document.items[0].style == 2,
                "Selected arrow style did not update in place.");
        require(app.document.undo() && app.document.items[0].style == 0, "Style undo failed.");
        app.document.selected = 0;
        require(app.image.pixels == originalPixels && app.view.origin == originalView.origin &&
                    app.view.scale == originalView.scale,
                "Editing properties changed capture or viewport.");
        require(saveToolPreferences(), "Cannot save opacity preferences.");
        app.opacities.fill(0);
        loadToolPreferences();
        require(std::abs(app.opacities[static_cast<size_t>(Tool::Arrow)] - .5f) < .02f,
                "Opacity preferences did not restore independently of annotation undo.");
        // Each tool's contextual controls remain inside their own regions at all supported DPIs.
        for (float dpi : {1.0f, 1.5f, 2.0f})
            for (Point size : {Point{850, 430}, Point{1050, 740}, Point{1280, 840}})
            {
                app.dpi = dpi;
                SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(size.x * dpi),
                             static_cast<int>(size.y * dpi), SWP_NOZORDER | SWP_NOACTIVATE);
                for (Tool tool : {Tool::Select, Tool::Pen, Tool::Highlight, Tool::Text, Tool::Arrow,
                                  Tool::Circle, Tool::Check, Tool::Line})
                {
                    app.tool = tool;
                    app.document.selected = -1;
                    app.inspectorScroll = 0;
                    updateView();
                    buildButtons();
                    const auto c = clientDips(), viewport = canvasRect();
                    for (const auto &b : app.buttons)
                    {
                        require(b.rect.left >= 0 && b.rect.top >= 0 &&
                                    b.rect.right <= c.right + .1f &&
                                    b.rect.bottom <= c.bottom + .1f,
                                "A UI control is clipped by the window.");
                        if (propertyCommand(b.command) && b.rect.left >= viewport.left)
                            require(b.rect.left >= viewport.right,
                                    "Properties overlap the screenshot.");
                    }
                    saveBytes(L"ui-layout-" + std::to_wstring(static_cast<int>(dpi * 100)) + L"-" +
                                  std::to_wstring(static_cast<int>(size.x)) + L"-" +
                                  ToolNames[static_cast<int>(tool)] + L".png",
                              app.graphics.png(renderEditorPreview()));
                }
            }
        app.dpi = 1;
        SetWindowPos(app.window, nullptr, 0, 0, 1280, 840, SWP_NOZORDER | SWP_NOACTIVATE);
        app.tool = Tool::Select;
        app.document.selected = 0;
        app.inspectorScroll = 0;
        saveBytes(L"ui-editor.png", app.graphics.png(renderEditorPreview()));
        // A large custom palette can be scrolled without changing the screenshot geometry.
        for (int i = 0; i < 56; ++i)
            app.palette.push_back(rgb(i * 3, i * 2, i));
        app.inspectorScroll = inspectorLayout().maxScroll;
        buildButtons();
        require(inspectorLayout().maxScroll > 0 && button(OpacitySlider).left >= canvasRect().right,
                "Long inspector did not scroll to opacity controls.");
        app.palette.assign(Palette.begin(), Palette.end());
        app.inspectorScroll = 0;
        command(ToggleFormatting);
        require(canvasRect().right == clientDips().right,
                "Closing inspector did not return canvas space.");
        command(ToggleFormatting);
        app.image = {};
        app.document.clear();
        app.tool = Tool::Select;
        saveBytes(L"ui-empty.png", app.graphics.png(renderEditorPreview()));
        std::cout << "PASS: complete settings/menus and repeated gear popup, native UI layout at "
                     "100/150/200% DPI, compact bounds, preset/custom "
                     "stroke sizes, single-step undo/redo, cancellation, opacity export/PNG, "
                     "in-place style edits, viewport stability, scrolling and collapse.\n";
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
    if (originalDesktop)
        SetThreadDesktop(originalDesktop);
    if (desktop)
        CloseDesktop(desktop);
    if (originalStation)
        SetProcessWindowStation(originalStation);
    if (station)
        CloseWindowStation(station);
    return result;
}
