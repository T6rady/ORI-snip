// UI colors never enter the image/export pipeline or replace annotation colors.
constexpr std::array<Color, 4> ThemeAccents = {ClassicAccent, OrangeAccent, rgb(37, 99, 235),
                                               rgb(0, 133, 119)};
constexpr std::array<const wchar_t *, 4> ThemeNames = {L"Purple", L"Orange", L"Blue", L"Teal"};
Color mixColor(Color a, Color b, float amount)
{
    auto component = [&](int shift) {
        return static_cast<unsigned>(
            std::lround(((a >> shift) & 255) * (1 - amount) + ((b >> shift) & 255) * amount));
    };
    return rgb(component(0), component(8), component(16));
}
Color uiSurface()
{
    return app.darkTheme ? rgb(30, 35, 43) : rgb(255, 255, 255);
}
Color uiRaised()
{
    return app.darkTheme ? rgb(39, 45, 55) : rgb(247, 248, 250);
}
Color uiCanvas()
{
    return app.darkTheme ? rgb(20, 24, 31) : rgb(246, 247, 251);
}
Color uiBorder()
{
    return app.darkTheme ? rgb(58, 67, 81) : rgb(224, 228, 233);
}
Color uiSelected()
{
    return mixColor(uiSurface(), Accent, app.darkTheme ? .22f : .10f);
}
Color uiSelectedBorder()
{
    return mixColor(uiSurface(), Accent, .46f);
}
Color uiPrimary()
{
    return app.darkTheme ? rgb(46, 55, 68) : rgb(32, 38, 46);
}
Color uiSolidAccent()
{
    // White labels need more contrast than the orange used for small accents.
    return app.colorTheme == 1 ? rgb(172, 73, 0) : ThemeAccents[app.colorTheme];
}
Color uiAccentText()
{
    return app.darkTheme ? Accent : uiSolidAccent();
}
Color themeSurfaceColor(Color c)
{
    if (c == Accent)
        return c;
    for (Color tint : {rgb(233, 226, 255), rgb(242, 238, 255), rgb(249, 248, 255),
                       rgb(225, 218, 249), rgb(255, 239, 225), rgb(255, 247, 240),
                       rgb(255, 244, 234), rgb(255, 226, 201), rgb(229, 248, 238)})
        if (c == tint)
            return uiSelected();
    if (c == rgb(219, 211, 248) || c == rgb(255, 194, 143))
        return uiSelectedBorder();
    if (c == rgb(93, 57, 216))
        return mixColor(Accent, rgb(0, 0, 0), .12f);
    if (c == rgb(81, 44, 199))
        return mixColor(Accent, rgb(0, 0, 0), .22f);
    if (c == rgb(255, 255, 255))
        return uiSurface();
    for (Color border : {rgb(218, 221, 232), rgb(231, 233, 241), rgb(227, 229, 238),
                         rgb(226, 229, 237), rgb(229, 225, 243)})
        if (c == border)
            return uiBorder();
    if (app.darkTheme)
    {
        const unsigned r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
        if (std::min({r, g, b}) > 242)
            return uiRaised();
        if (std::min({r, g, b}) > 185)
            return uiBorder();
    }
    return c;
}
void updateInterfaceColors()
{
    Accent = ThemeAccents[app.colorTheme];
    // Lift dark accents for text/icons; keep the same preset hue in both modes.
    if (app.darkTheme)
        Accent = mixColor(Accent, rgb(255, 255, 255), .23f);
    Ink = app.darkTheme ? rgb(233, 237, 244) : rgb(32, 38, 46);
    Muted = app.darkTheme ? rgb(158, 170, 188) : rgb(112, 121, 135);
}

// Keep the native top menu's labels and submenus, while matching its bar to Dark.
constexpr ULONG_PTR RootMenuTag = 0xC0D000;
constexpr std::array<const wchar_t *, 5> RootMenuLabels = {L"File", L"Edit", L"View", L"Settings",
                                                         L"Help"};
bool rootMenuItem(ULONG_PTR data)
{
    return data >= RootMenuTag && data < RootMenuTag + RootMenuLabels.size();
}
HFONT rootMenuFont()
{
    return CreateFontW(-static_cast<int>(16 * app.dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}
void applyWindowTheme()
{
    const BOOL dark = app.darkTheme;
    DwmSetWindowAttribute(app.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    HMENU menu = app.windowedMenu ? app.windowedMenu : GetMenu(app.window);
    if (!menu)
        return;
    auto background = app.darkTheme ? CreateSolidBrush(uiSurface()) : nullptr;
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = background;
    SetMenuInfo(menu, &info);
    if (app.menuBackground)
        DeleteObject(app.menuBackground);
    app.menuBackground = background;
    for (UINT i = 0; i < RootMenuLabels.size(); ++i)
    {
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_FTYPE | MIIM_DATA;
        item.fType = app.darkTheme ? MFT_OWNERDRAW : MFT_STRING;
        item.dwItemData = app.darkTheme ? RootMenuTag + i : 0;
        SetMenuItemInfoW(menu, i, TRUE, &item);
    }
    DrawMenuBar(app.window);
}
void measureRootMenu(MEASUREITEMSTRUCT &item)
{
    HDC dc = GetDC(app.window);
    auto font = rootMenuFont();
    auto old = SelectObject(dc, font);
    const auto label = RootMenuLabels[item.itemData - RootMenuTag];
    SIZE size{};
    GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &size);
    item.itemWidth = size.cx + static_cast<UINT>(14 * app.dpi);
    item.itemHeight = size.cy + static_cast<UINT>(4 * app.dpi);
    SelectObject(dc, old);
    DeleteObject(font);
    ReleaseDC(app.window, dc);
}
void drawRootMenu(const DRAWITEMSTRUCT &item)
{
    const bool on = item.itemState & (ODS_SELECTED | ODS_HOTLIGHT);
    SetDCBrushColor(item.hDC, on ? uiSelected() : uiSurface());
    FillRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, Ink);
    auto font = rootMenuFont();
    auto old = SelectObject(item.hDC, font);
    auto rect = item.rcItem;
    DrawTextW(item.hDC, RootMenuLabels[item.itemData - RootMenuTag], -1, &rect,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(item.hDC, old);
    DeleteObject(font);
}
