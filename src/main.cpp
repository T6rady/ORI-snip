#include "graphics.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <fstream>
#include <array>
#include <string>
#include <stdexcept>

using namespace snip;
namespace
{
constexpr wchar_t MainClass[] = L"JackSnip.Main.1", OverlayClass[] = L"JackSnip.Capture.1",
                  SettingsClass[] = L"JackSnip.Settings.1";
constexpr UINT TrayMessage = WM_APP + 20, LaunchMessage = WM_APP + 21;
constexpr UINT CaptureTimer = 1, StatusTimer = 2, SmokeTimer = 3;
constexpr float ToolbarHeight = 166, StatusHeight = 32;
constexpr Color Accent = rgb(108, 72, 231), Ink = rgb(35, 39, 56), Muted = rgb(123, 128, 147);
enum Command
{
    NewSnip = 1001,
    Copy,
    Save,
    SaveAs,
    Exit,
    Undo,
    Redo,
    DeleteSelected,
    Clear,
    Fit,
    Actual,
    SelectTool,
    PenTool,
    CircleTool,
    ArrowTool,
    CheckTool,
    LineTool,
    CustomColor,
    SizeDown,
    SizeUp,
    Settings,
    Startup,
    About,
    ColorFirst = 1100,
    ShowEditor = 1200,
    CircleStyleMenu = 1300,
    ArrowStyleMenu,
    CheckStyleMenu,
    LineStyleMenu,
    StyleChoiceFirst = 1400
};
const std::array<Color, 8> Palette = {rgb(239, 68, 68),   rgb(249, 115, 22), rgb(250, 204, 21),
                                      rgb(34, 197, 94),   rgb(14, 165, 233), rgb(168, 85, 247),
                                      rgb(255, 255, 255), rgb(15, 23, 42)};
constexpr std::array<uint8_t, 6> StyleCounts = {1, 1, 3, 4, 3, 3};
constexpr const wchar_t *ToolNames[] = {L"Select", L"Pen", L"Circle", L"Arrow", L"Check", L"Line"};
constexpr int styleCommand(Tool tool, int style)
{
    return StyleChoiceFirst + (static_cast<int>(tool) - static_cast<int>(Tool::Circle)) * 4 + style;
}
struct Button
{
    Rect rect;
    int command;
    std::wstring label;
};
enum class Drag
{
    None,
    Draw,
    Move,
    Resize,
    Endpoint,
    Pan
};
struct Application
{
    HINSTANCE instance = nullptr;
    HWND window = nullptr, overlay = nullptr, settingsWindow = nullptr, hotkeyControl = nullptr,
         tooltip = nullptr;
    HDC overlayDC = nullptr;
    HBITMAP overlaySurface = nullptr;
    HGDIOBJ overlayPrevious = nullptr;
    HFONT dialogFont = nullptr;
    Graphics graphics;
    Com<ID2D1HwndRenderTarget> target;
    Com<ID2D1Bitmap> displayBitmap;
    Bitmap image, desktop, dimDesktop;
    Document document;
    Tool tool = Tool::Select;
    std::array<Color, 6> colors = {Palette[0], Palette[0], Palette[0],
                                   Palette[0], Palette[3], Palette[0]};
    std::array<uint8_t, 6> styles{};
    float thickness = 4, dpi = 1;
    View view;
    bool fit = true, dirty = false, capturePending = false, exiting = false, tray = false,
         spaceDown = false;
    bool selecting = false, changed = false, smoke = false;
    bool toolPreferencesDirty = false;
    Drag drag = Drag::None;
    Point dragStart, panStart;
    Annotation before;
    int handle = -1, virtualX = 0, virtualY = 0;
    POINT selectionStart{}, selectionEnd{};
    std::wstring iniPath, savePath, status;
    WORD hotkey = MAKEWORD('S', HOTKEYF_CONTROL | HOTKEYF_ALT);
    int hotkeyId = 1;
    bool hotkeyRegistered = false;
    std::vector<Button> buttons;
    int hover = 0, pressed = 0;
    size_t tooltipCount = 0;
    UINT taskbarCreated = 0;
} app;

LRESULT CALLBACK mainProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK overlayProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK settingsProcedure(HWND, UINT, WPARAM, LPARAM);
void command(int id);
void startSnip(bool instant = false);
void hideEditorForCapture();
void openOverlay();
void repaint()
{
    if (app.window)
        InvalidateRect(app.window, nullptr, FALSE);
}
void error(HWND owner, const char *text)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring message(static_cast<size_t>(std::max(1, count)), 0);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, message.data(), count);
    MessageBoxW(owner, message.c_str(), L"Snipper", MB_OK | MB_ICONERROR);
}
void status(const std::wstring &text)
{
    app.status = text;
    SetTimer(app.window, StatusTimer, 4500, nullptr);
    repaint();
}
float dpiFor(HWND hwnd)
{
    return GetDpiForWindow(hwnd) / 96.0f;
}
Rect clientDips()
{
    RECT r{};
    GetClientRect(app.window, &r);
    return {0, 0, r.right / app.dpi, r.bottom / app.dpi};
}
Rect canvasRect()
{
    auto r = clientDips();
    return {0, ToolbarHeight, r.right, std::max(ToolbarHeight, r.bottom - StatusHeight)};
}
bool hasImage()
{
    return !app.image.empty();
}
bool selected()
{
    return app.document.selected >= 0 &&
           app.document.selected < static_cast<int>(app.document.items.size());
}
Color activeColor()
{
    return selected() ? app.document.items[app.document.selected].color
                      : app.colors[static_cast<size_t>(app.tool)];
}
void loadToolPreferences()
{
    for (size_t i = 0; i < app.colors.size(); ++i)
    {
        const std::wstring colorKey = std::wstring(ToolNames[i]) + L"Color";
        const std::wstring styleKey = std::wstring(ToolNames[i]) + L"Style";
        const UINT value = GetPrivateProfileIntW(L"ToolPreferences", colorKey.c_str(),
                                                app.colors[i], app.iniPath.c_str());
        const UINT style = GetPrivateProfileIntW(L"ToolPreferences", styleKey.c_str(),
                                                app.styles[i], app.iniPath.c_str());
        if (value <= 0xFFFFFF)
            app.colors[i] = value;
        if (style < StyleCounts[i])
            app.styles[i] = static_cast<uint8_t>(style);
    }
    app.toolPreferencesDirty = false;
}
bool saveToolPreferences()
{
    if (!app.toolPreferencesDirty)
        return true;
    // A single small section write on close/exit, never on a drawing or rendering event.
    std::wstring section;
    for (size_t i = 0; i < app.colors.size(); ++i)
    {
        section += std::wstring(ToolNames[i]) + L"Color=" + std::to_wstring(app.colors[i]);
        section.push_back(L'\0');
        section += std::wstring(ToolNames[i]) + L"Style=" + std::to_wstring(app.styles[i]);
        section.push_back(L'\0');
    }
    section.push_back(L'\0');
    if (!WritePrivateProfileSectionW(L"ToolPreferences", section.c_str(), app.iniPath.c_str()))
        return false;
    app.toolPreferencesDirty = false;
    return true;
}
void saveToolPreferencesOrNotify()
{
    if (!saveToolPreferences())
        error(app.window, "Tool colors and styles could not be saved. Keep Snipper in a writable folder.");
}
std::wstring hotkeyName(WORD value)
{
    std::wstring result;
    BYTE modifiers = HIBYTE(value), key = LOBYTE(value);
    if (modifiers & HOTKEYF_CONTROL)
        result += L"Ctrl+";
    if (modifiers & HOTKEYF_ALT)
        result += L"Alt+";
    if (modifiers & HOTKEYF_SHIFT)
        result += L"Shift+";
    if (!key)
        return L"Disabled";
    if (key >= 'A' && key <= 'Z')
        result += static_cast<wchar_t>(key);
    else if (key >= '0' && key <= '9')
        result += static_cast<wchar_t>(key);
    else if (key >= VK_F1 && key <= VK_F24)
        result += L"F" + std::to_wstring(key - VK_F1 + 1);
    else
    {
        wchar_t name[64]{};
        LONG scan = static_cast<LONG>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16);
        if (modifiers & HOTKEYF_EXT)
            scan |= 1 << 24;
        GetKeyNameTextW(scan, name, 64);
        result += name;
    }
    return result;
}
UINT hotkeyModifiers(WORD value)
{
    UINT modifiers = MOD_NOREPEAT;
    BYTE flags = HIBYTE(value);
    if (flags & HOTKEYF_CONTROL)
        modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT)
        modifiers |= MOD_ALT;
    if (flags & HOTKEYF_SHIFT)
        modifiers |= MOD_SHIFT;
    return modifiers;
}
bool registerShortcut(WORD value)
{
    if (value == app.hotkey && app.hotkeyRegistered)
        return true;
    BYTE key = LOBYTE(value), flags = HIBYTE(value);
    if (key && (!(flags & (HOTKEYF_CONTROL | HOTKEYF_ALT)) || key == VK_F12))
    {
        MessageBoxW(app.settingsWindow ? app.settingsWindow : app.window,
                    L"Use a shortcut with Ctrl or Alt. F12 is reserved by Windows.",
                    L"Choose a different shortcut", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    int nextId = app.hotkeyId == 1 ? 2 : 1;
    if (key && !RegisterHotKey(app.window, nextId, hotkeyModifiers(value), key))
    {
        MessageBoxW(
            app.settingsWindow ? app.settingsWindow : app.window,
            L"That shortcut is already in use or reserved by Windows. Choose another combination.",
            L"Shortcut unavailable", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    if (app.hotkeyRegistered)
        UnregisterHotKey(app.window, app.hotkeyId);
    app.hotkey = value;
    app.hotkeyId = nextId;
    app.hotkeyRegistered = key != 0;
    return true;
}
std::wstring executablePath()
{
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return path;
}
bool startupEnabled()
{
    HKEY key = nullptr;
    wchar_t value[32768]{};
    DWORD size = sizeof(value), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LONG result =
        RegQueryValueExW(key, L"JackSnip", nullptr, &type, reinterpret_cast<BYTE *>(value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ &&
           std::wstring(value) == L"\"" + executablePath() + L"\" --tray";
}
void toggleStartup()
{
    bool enabled = startupEnabled();
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        throw std::runtime_error("Windows did not allow changing the startup setting.");
    std::wstring value = L"\"" + executablePath() + L"\" --tray";
    LONG result = enabled
                      ? RegDeleteValueW(key, L"JackSnip")
                      : RegSetValueExW(key, L"JackSnip", 0, REG_SZ,
                                       reinterpret_cast<const BYTE *>(value.c_str()),
                                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Could not update the startup setting.");
    status(enabled ? L"Run at sign-in disabled" : L"Snipper will start quietly at sign-in");
}
void addTray()
{
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = app.window;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = TrayMessage;
    data.hIcon = LoadIconW(app.instance, MAKEINTRESOURCEW(101));
    wcscpy_s(data.szTip, L"Snipper - click to snip; right-click for menu");
    app.tray = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}
void removeTray()
{
    if (app.tray)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = app.window;
        data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
        app.tray = false;
    }
}
void showEditor()
{
    // Undo capture-time cloaking before restoring the editor.
    const BOOL uncloaked = FALSE;
    DwmSetWindowAttribute(app.window, DWMWA_CLOAK, &uncloaked, sizeof(uncloaked));
    ShowWindow(app.window, SW_RESTORE);
    SetForegroundWindow(app.window);
    repaint();
}
void updateTitle()
{
    std::wstring title = L"Snipper";
    if (hasImage())
        title += L"  |  " + std::to_wstring(app.image.width) + L" x " +
                 std::to_wstring(app.image.height) + (app.dirty ? L"  *" : L"");
    SetWindowTextW(app.window, title.c_str());
}
class CaptureDialogGuard
{
    HHOOK hook = nullptr;
    static LRESULT CALLBACK prepareDialog(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HCBT_ACTIVATE || code == HCBT_DESTROYWND)
        {
            HWND window = reinterpret_cast<HWND>(wp);
            wchar_t name[80]{};
            GetClassNameW(window, name, 80);
            if (wcscmp(name, L"#32770") == 0)
            {
                const BOOL disabled = TRUE;
                DwmSetWindowAttribute(window, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled,
                                      sizeof(disabled));
                // Also remove the dialog's compositor surface before destruction so
                // an immediately following capture cannot sample its closing animation.
                if (code == HCBT_DESTROYWND)
                    DwmSetWindowAttribute(window, DWMWA_CLOAK, &disabled, sizeof(disabled));
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

  public:
    explicit CaptureDialogGuard(bool enabled)
    {
        if (enabled)
        {
            hook = SetWindowsHookExW(WH_CBT, prepareDialog, nullptr, GetCurrentThreadId());
            if (!hook)
                throw std::runtime_error("Cannot prepare save dialogs for capture.");
        }
    }
    ~CaptureDialogGuard()
    {
        if (hook)
            UnhookWindowsHookEx(hook);
    }
    CaptureDialogGuard(const CaptureDialogGuard &) = delete;
    CaptureDialogGuard &operator=(const CaptureDialogGuard &) = delete;
};
bool canDiscard(bool takingSnip = false)
{
    if (!app.dirty)
        return true;
    CaptureDialogGuard dialogs(takingSnip);
    const wchar_t *question = takingSnip ? L"Save changes before taking a new snip?"
                                         : L"Save changes before closing Snipper?";
    const int result =
        MessageBoxW(app.window, question, L"Snipper", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (result == IDCANCEL)
    {
        if (takingSnip)
            showEditor();
        return false;
    }
    if (result == IDNO)
        return true;
    try
    {
        command(Save);
    }
    catch (...)
    {
        if (takingSnip)
            showEditor();
        throw;
    }
    if (app.dirty && takingSnip)
        showEditor();
    return !app.dirty;
}
void releaseImage()
{
    app.document.clear();
    app.image = {};
    app.displayBitmap.reset();
    app.savePath.clear();
    app.dirty = false;
    app.fit = true;
    app.view = {};
    app.target.reset();
    app.status.clear();
    updateTitle();
}
void updateView()
{
    if (!hasImage())
        return;
    auto r = canvasRect();
    float width = std::max(1.0f, r.width() - 40), height = std::max(1.0f, r.height() - 40);
    if (app.fit)
    {
        app.view.scale = std::max(
            .0001f, std::min({width / app.image.width, height / app.image.height, 1 / app.dpi}));
        app.view.origin = {(r.width() - app.image.width * app.view.scale) / 2,
                           r.top + (r.height() - app.image.height * app.view.scale) / 2};
    }
}
Point limited(Point p)
{
    return {std::clamp(p.x, 0.0f, static_cast<float>(app.image.width)),
            std::clamp(p.y, 0.0f, static_cast<float>(app.image.height))};
}
std::vector<Point> handles(const Annotation &item)
{
    if (item.kind == Tool::Arrow || item.kind == Tool::Line)
        return {item.a, item.b};
    auto r = item.bounds();
    return {{r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}};
}
struct ToolbarLayout
{
    Rect draw, shapes;
};
ToolbarLayout toolbarLayout()
{
    const float width = clientDips().right;
    return {{20, 75, 218, 119}, {width - 504, 75, width - 20, 119}};
}
void buildButtons()
{
    app.buttons.clear();
    const auto layout = toolbarLayout();
    float x = 20;
    auto add = [&](int id, const wchar_t *text, float width, float y, float height = 36) {
        app.buttons.push_back({{x, y, x + width, y + height}, id, text});
        x += width + 4;
    };
    auto addStyleTool = [&](int id, const wchar_t *text, int menu) {
        add(id, text, 84, 79);
        x -= 4;
        add(menu, L"", 24, 79);
        x += 8;
    };
    add(NewSnip, L"New snip", 142, 11);
    x = 194;
    add(Undo, L"", 36, 11);
    x += 8;
    add(Redo, L"", 36, 11);
    x = clientDips().right - 226;
    add(Copy, L"Copy", 114, 11);
    x += 6;
    add(Save, L"Save", 82, 11);
    x = layout.draw.left + 8;
    add(SelectTool, L"Select", 90, 79);
    x += 8;
    add(PenTool, L"Pen", 80, 79);
    x = layout.shapes.left + 8;
    addStyleTool(CircleTool, L"Circle", CircleStyleMenu);
    addStyleTool(ArrowTool, L"Arrow", ArrowStyleMenu);
    addStyleTool(CheckTool, L"Check", CheckStyleMenu);
    addStyleTool(LineTool, L"Line", LineStyleMenu);
    x = 70;
    for (int i = 0; i < 8; ++i)
    {
        app.buttons.push_back({{x, 132, x + 24, 156}, ColorFirst + i, L""});
        x += 34;
    }
    x = 350;
    add(CustomColor, L"", 28, 130, 28);
    x = 472;
    add(SizeDown, L"\u2212", 28, 130, 28);
    x += 48;
    add(SizeUp, L"+", 28, 130, 28);
    x = clientDips().right - 140;
    add(Fit, L"Fit", 48, 130, 28);
    add(Actual, L"100%", 64, 130, 28);
    if (!hasImage())
    {
        auto canvas = canvasRect();
        float middle = canvas.top + canvas.height() / 2;
        x = clientDips().right / 2 - 76;
        add(NewSnip, L"Take a snip", 152, middle + 30, 42);
    }

    // Keep native tooltip hit areas in physical pixels as the window moves between displays.
    if (app.tooltip)
    {
        if (app.tooltipCount != app.buttons.size())
        {
            for (size_t i = 0; i < app.tooltipCount; ++i)
            {
                TOOLINFOW info{};
                info.cbSize = sizeof(info);
                info.hwnd = app.window;
                info.uId = i + 1;
                SendMessageW(app.tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
            }
            app.tooltipCount = 0;
        }
        for (size_t i = 0; i < app.buttons.size(); ++i)
        {
            const auto &b = app.buttons[i];
            const wchar_t *hint = L"";
            switch (b.command)
            {
            case NewSnip:
                hint = L"Capture an area (Ctrl+N)";
                break;
            case Copy:
                hint = L"Copy image with annotations (Ctrl+C)";
                break;
            case Save:
                hint = L"Save PNG (Ctrl+S)";
                break;
            case SelectTool:
                hint = L"Select, move, and resize (V)";
                break;
            case PenTool:
                hint = L"Freehand pen (P)";
                break;
            case CircleTool:
                hint = L"Circle (O) - hold Shift for a perfect circle";
                break;
            case ArrowTool:
                hint = L"Arrow (A)";
                break;
            case CheckTool:
                hint = L"Check sticker (K)";
                break;
            case LineTool:
                hint = L"Line (L) - hold Shift to snap the angle";
                break;
            case CircleStyleMenu:
            case ArrowStyleMenu:
            case CheckStyleMenu:
            case LineStyleMenu:
                hint = L"Choose a shape style";
                break;
            case Undo:
                hint = L"Undo (Ctrl+Z)";
                break;
            case Redo:
                hint = L"Redo (Ctrl+Y)";
                break;
            case CustomColor:
                hint = L"Choose a custom color";
                break;
            case SizeDown:
                hint = L"Thinner stroke ([)";
                break;
            case SizeUp:
                hint = L"Thicker stroke (])";
                break;
            case Fit:
                hint = L"Fit image to the window";
                break;
            case Actual:
                hint = L"View at original size";
                break;
            default: {
                static const wchar_t *names[] = {L"Coral", L"Orange", L"Yellow", L"Green",
                                                 L"Sky",   L"Violet", L"White",  L"Ink"};
                if (b.command >= ColorFirst && b.command < ColorFirst + 8)
                    hint = names[b.command - ColorFirst];
            }
            }
            TOOLINFOW info{};
            info.cbSize = sizeof(info);
            info.hwnd = app.window;
            info.uId = i + 1;
            info.uFlags = TTF_SUBCLASS;
            info.rect = {static_cast<LONG>(b.rect.left * app.dpi),
                         static_cast<LONG>(b.rect.top * app.dpi),
                         static_cast<LONG>(b.rect.right * app.dpi),
                         static_cast<LONG>(b.rect.bottom * app.dpi)};
            info.lpszText = const_cast<wchar_t *>(hint);
            SendMessageW(app.tooltip, app.tooltipCount ? TTM_NEWTOOLRECTW : TTM_ADDTOOLW, 0,
                         reinterpret_cast<LPARAM>(&info));
        }
        app.tooltipCount = app.buttons.size();
    }
}
bool enabled(int id)
{
    if (id == Undo || id == Redo)
        return hasImage() && (id == Undo ? app.document.canUndo() : app.document.canRedo());
    if (id == NewSnip)
        return !app.capturePending && !app.overlay;
    if (id == Copy || id == Save || id == SaveAs || id == Fit || id == Actual ||
        (id >= SelectTool && id <= LineTool) || (id >= CircleStyleMenu && id <= LineStyleMenu))
        return hasImage();
    return true;
}
bool active(int id)
{
    if (hasImage() && (id == Fit || id == Actual))
        return id == Fit ? app.fit : !app.fit && std::abs(app.view.scale * app.dpi - 1) < .001f;
    return id >= SelectTool && id <= LineTool && static_cast<int>(app.tool) == id - SelectTool;
}
void ensureTarget()
{
    if (app.target)
        return;
    app.graphics.initialize();
    RECT rect{};
    GetClientRect(app.window, &rect);
    auto properties = D2D1::RenderTargetProperties();
    properties.dpiX = properties.dpiY = app.dpi * 96;
    check(app.graphics.factory->CreateHwndRenderTarget(
              properties,
              D2D1::HwndRenderTargetProperties(
                  app.window, D2D1::SizeU(std::max(1L, rect.right), std::max(1L, rect.bottom))),
              app.target.put()),
          "Cannot initialize the editor renderer.");
}
void drawUIIcon(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush, int id, Point origin,
                Color foreground)
{
    brush->SetColor(color(foreground));
    auto line = [&](float x1, float y1, float x2, float y2) {
        rt->DrawLine({origin.x + x1, origin.y + y1}, {origin.x + x2, origin.y + y2}, brush, 1.7f,
                     app.graphics.roundStroke.get());
    };
    auto box = [&](float x1, float y1, float x2, float y2) {
        rt->DrawRoundedRectangle(
            D2D1::RoundedRect({origin.x + x1, origin.y + y1, origin.x + x2, origin.y + y2}, 2, 2),
            brush, 1.7f);
    };
    switch (id)
    {
    case NewSnip:
        line(2, 7, 2, 2);
        line(2, 2, 7, 2);
        line(13, 2, 18, 2);
        line(18, 2, 18, 7);
        line(18, 13, 18, 18);
        line(18, 18, 13, 18);
        line(7, 18, 2, 18);
        line(2, 18, 2, 13);
        line(10, 6, 10, 14);
        line(6, 10, 14, 10);
        break;
    case Copy:
        box(7, 7, 18, 18);
        line(12, 3, 3, 3);
        line(3, 3, 3, 12);
        break;
    case Save:
        line(10, 2, 10, 13);
        line(6, 9, 10, 13);
        line(10, 13, 14, 9);
        line(3, 14, 3, 18);
        line(3, 18, 17, 18);
        line(17, 18, 17, 14);
        break;
    case SelectTool:
        line(4, 2, 4, 17);
        line(4, 2, 16, 11);
        line(16, 11, 10, 12);
        line(10, 12, 7, 17);
        line(10, 12, 14, 18);
        break;
    case PenTool:
        line(4, 12, 13, 3);
        line(13, 3, 17, 7);
        line(17, 7, 8, 16);
        line(8, 16, 3, 17);
        line(3, 17, 4, 12);
        line(11, 5, 15, 9);
        break;
    case Undo:
    case Redo: {
        const bool redo = id == Redo;
        auto p = [&](float x, float y) {
            return D2D1::Point2F(origin.x + (redo ? 20 - x : x), origin.y + y);
        };
        Com<ID2D1PathGeometry> path;
        Com<ID2D1GeometrySink> sink;
        check(app.graphics.factory->CreatePathGeometry(path.put()), "Cannot draw history icon.");
        check(path->Open(sink.put()), "Cannot draw history icon.");
        sink->BeginFigure(p(4, 7), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddBezier(D2D1::BezierSegment(p(19, 2), p(21, 18), p(9, 17)));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        check(sink->Close(), "Cannot finish history icon.");
        rt->DrawGeometry(path.get(), brush, 1.7f, app.graphics.roundStroke.get());
        auto a = p(4, 7), b = p(8, 3), c = p(9, 10);
        rt->DrawLine(a, b, brush, 1.7f, app.graphics.roundStroke.get());
        rt->DrawLine(a, c, brush, 1.7f, app.graphics.roundStroke.get());
        break;
    }
    case CustomColor:
        line(10, 5, 10, 15);
        line(5, 10, 15, 10);
        break;
    default:
        break;
    }
}
void paintEditor(ID2D1RenderTarget *alternate = nullptr)
{
    if (!alternate)
        ensureTarget();
    updateView();
    buildButtons();
    ID2D1RenderTarget *rt = alternate ? alternate : app.target.get();
    auto client = clientDips(), canvas = canvasRect();
    Com<ID2D1Bitmap> alternateBitmap;
    Com<ID2D1SolidColorBrush> brush;
    check(rt->CreateSolidColorBrush(color(rgb(15, 23, 42)), brush.put()),
          "Cannot paint the editor.");
    auto fill = [&](Rect r, Color c) {
        brush->SetColor(color(c));
        rt->FillRectangle({r.left, r.top, r.right, r.bottom}, brush.get());
    };
    auto text = [&](const std::wstring &s, Rect r, Color c, IDWriteTextFormat *font,
                    bool centered = false) {
        brush->SetColor(color(c));
        font->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER
                                        : DWRITE_TEXT_ALIGNMENT_LEADING);
        rt->DrawText(s.c_str(), static_cast<UINT32>(s.size()), font,
                     {r.left, r.top, r.right, r.bottom}, brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    auto rounded = [&](Rect r, Color c, float radius = 10) {
        brush->SetColor(color(c));
        rt->FillRoundedRectangle(
            D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, radius, radius), brush.get());
    };
    auto divider = [&](float x, float top, float bottom) {
        fill({x, top, x + 1, bottom}, rgb(218, 221, 232));
    };
    auto panel = [&](Rect r, Color background, Color border) {
        rounded(r, background, 10);
        brush->SetColor(color(border));
        rt->DrawRoundedRectangle(D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, 10, 10),
                                 brush.get(), 1);
    };
    rt->BeginDraw();
    rt->Clear(color(rgb(246, 247, 251)));
    rt->PushAxisAlignedClip({canvas.left, canvas.top, canvas.right, canvas.bottom},
                            D2D1_ANTIALIAS_MODE_ALIASED);
    brush->SetColor(color(rgb(222, 225, 236)));
    for (float y = canvas.top + 18; y < canvas.bottom; y += 24)
        for (float x = 18; x < canvas.right; x += 24)
            rt->FillEllipse(D2D1::Ellipse({x, y}, .8f, .8f), brush.get());
    rt->PopAxisAlignedClip();
    fill({0, 0, client.right, ToolbarHeight}, rgb(255, 255, 255));
    fill({0, 58, client.right, 59}, rgb(237, 238, 244));
    fill({0, 123, client.right, ToolbarHeight}, rgb(250, 250, 253));
    fill({0, ToolbarHeight - 1, client.right, ToolbarHeight}, rgb(227, 229, 238));
    // The capture icon and action are one button; history uses the space it frees.
    divider(180, 19, 39);
    if (hasImage() && client.right > 960)
        text(std::to_wstring(app.image.width) + L" \u00D7 " + std::to_wstring(app.image.height),
             {client.right - 365, 11, client.right - 238, 47}, Muted, app.graphics.smallFont.get(),
             true);
    const auto layout = toolbarLayout();
    text(L"DRAW", {layout.draw.left + 2, 61, layout.draw.right, 73}, Muted,
         app.graphics.labelFont.get());
    text(L"SHAPES", {layout.shapes.left + 2, 61, layout.shapes.right, 73}, Muted,
         app.graphics.labelFont.get());
    divider((layout.draw.right + layout.shapes.left) / 2, 77, 117);
    panel(layout.draw, rgb(249, 250, 252), rgb(226, 229, 237));
    panel(layout.shapes, rgb(249, 248, 255), rgb(229, 225, 243));
    text(L"Color", {22, 128, 66, 160}, Muted, app.graphics.smallFont.get());
    divider(399, 134, 154);
    text(L"Stroke", {420, 128, 465, 160}, Muted, app.graphics.smallFont.get());
    rounded({472, 130, 580, 158}, rgb(238, 239, 246), 8);
    divider(client.right - 170, 134, 154);
    rounded({client.right - 140, 130, client.right - 24, 158}, rgb(238, 239, 246), 8);
    for (size_t index = 0; index < app.buttons.size(); ++index)
    {
        const auto &button = app.buttons[index];
        auto r = button.rect;
        bool on = active(button.command) ||
                  (button.command == CircleStyleMenu && app.tool == Tool::Circle) ||
                  (button.command == ArrowStyleMenu && app.tool == Tool::Arrow) ||
                  (button.command == CheckStyleMenu && app.tool == Tool::Check) ||
                  (button.command == LineStyleMenu && app.tool == Tool::Line),
             over = app.hover == button.command, available = enabled(button.command);
        const bool down = app.pressed == static_cast<int>(index + 1) && over;
        const bool styleMenu = button.command >= CircleStyleMenu && button.command <= LineStyleMenu;
        if (down)
            r = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        if (button.command >= ColorFirst && button.command < ColorFirst + 8)
        {
            Color value = Palette[button.command - ColorFirst];
            bool chosen = activeColor() == value;
            float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            if (chosen || over)
            {
                brush->SetColor(color(chosen ? Accent : rgb(201, 204, 220)));
                rt->DrawEllipse(D2D1::Ellipse({cx, cy}, 12, 12), brush.get(), chosen ? 2 : 1);
            }
            brush->SetColor(color(value));
            rt->FillEllipse(D2D1::Ellipse({cx, cy}, 8.5f, 8.5f), brush.get());
            if (value == rgb(255, 255, 255))
            {
                brush->SetColor(color(rgb(212, 215, 229)));
                rt->DrawEllipse(D2D1::Ellipse({cx, cy}, 8.5f, 8.5f), brush.get(), 1);
            }
            continue;
        }
        const bool copied = button.command == Copy && app.status.find(L"Copied") == 0;
        const bool toolButton = button.command >= SelectTool && button.command <= LineTool;
        Color bg = button.command == NewSnip ? (over ? rgb(93, 57, 216) : Accent)
                   : copied                  ? rgb(229, 248, 238)
                   : on                      ? rgb(233, 226, 255)
                   : button.command == Copy  ? rgb(242, 238, 255)
                   : over && available       ? rgb(232, 234, 244)
                   : toolButton || styleMenu ? rgb(255, 255, 255)
                                             : rgb(245, 245, 250);
        if (down)
            bg = button.command == NewSnip ? rgb(81, 44, 199) : rgb(219, 211, 248);
        if (button.command == NewSnip)
            rounded({r.left, r.top + 2, r.right, r.bottom + 2}, rgb(225, 218, 249), 10);
        if (styleMenu || (button.command >= CircleTool && button.command <= LineTool))
        {
            // Split tools share one surface; their small chevron remains a separate hit target.
            Rect whole = styleMenu ? Rect{r.left - 84, r.top, r.right, r.bottom}
                                   : Rect{r.left, r.top, r.right + 24, r.bottom};
            rt->PushAxisAlignedClip({r.left, r.top, r.right, r.bottom},
                                    D2D1_ANTIALIAS_MODE_ALIASED);
            rounded(whole, bg, 8);
            rt->PopAxisAlignedClip();
        }
        else
            rounded(r, bg, 8);
        Color fg = !available                     ? rgb(174, 178, 192)
                   : button.command == NewSnip    ? rgb(255, 255, 255)
                   : copied                       ? rgb(35, 139, 87)
                   : on || button.command == Copy ? Accent
                                                  : Ink;
        if (styleMenu)
        {
            brush->SetColor(color(fg));
            float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            rt->DrawLine({cx - 3, cy - 1}, {cx, cy + 2}, brush.get(), 1.3f);
            rt->DrawLine({cx, cy + 2}, {cx + 3, cy - 1}, brush.get(), 1.3f);
        }
        else if (button.command >= CircleTool && button.command <= LineTool)
        {
            Annotation icon;
            icon.kind = static_cast<Tool>(button.command - SelectTool);
            icon.color = button.command == CheckTool && available ? Palette[3] : fg;
            icon.thickness = 1.7f;
            icon.style = app.styles[static_cast<size_t>(icon.kind)];
            icon.a = {r.left + 9, r.top + 10};
            icon.b = {r.left + 25, r.top + 26};
            if (icon.kind == Tool::Arrow || icon.kind == Tool::Line)
            {
                icon.a.y = r.top + 25;
                icon.b.y = r.top + 11;
            }
            app.graphics.drawAnnotations(rt, {icon});
            text(button.label, {r.left + 31, r.top, r.right, r.bottom}, fg,
                 app.graphics.font.get());
        }
        else if (button.command == NewSnip || button.command == Copy || button.command == Save ||
                 button.command == SelectTool || button.command == PenTool)
        {
            float inset = button.command == NewSnip ? 12 : 9;
            if (copied)
            {
                brush->SetColor(color(fg));
                rt->DrawLine({r.left + 12, r.top + 18}, {r.left + 17, r.top + 23}, brush.get(), 2);
                rt->DrawLine({r.left + 17, r.top + 23}, {r.left + 26, r.top + 13}, brush.get(), 2);
            }
            else
                drawUIIcon(rt, brush.get(), button.command,
                           {r.left + inset, (r.top + r.bottom) / 2 - 10}, fg);
            text(copied ? L"Copied!" : button.label,
                 {r.left + inset + 27, r.top, r.right - 4, r.bottom}, fg, app.graphics.font.get());
        }
        else if (button.command == Undo || button.command == Redo || button.command == CustomColor)
            drawUIIcon(rt, brush.get(), button.command,
                       {(r.left + r.right) / 2 - 10, (r.top + r.bottom) / 2 - 10}, fg);
        else
            text(button.label, r, fg, app.graphics.font.get(), true);
    }
    auto minus = std::find_if(app.buttons.begin(), app.buttons.end(),
                              [](const Button &b) { return b.command == SizeDown; });
    float size = selected() && app.document.items[app.document.selected].kind != Tool::Check
                     ? app.document.items[app.document.selected].thickness
                     : app.thickness;
    if (minus != app.buttons.end())
        text(std::to_wstring(static_cast<int>(size)) + L" px",
             {minus->rect.right + 2, 130, minus->rect.right + 52, 158}, Ink,
             app.graphics.smallFont.get(), true);
    if (hasImage())
    {
        auto &display = alternate ? alternateBitmap : app.displayBitmap;
        if (!display)
            check(rt->CreateBitmap(
                      D2D1::SizeU(app.image.width, app.image.height), app.image.pixels.data(),
                      app.image.width * 4,
                      D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                               D2D1_ALPHA_MODE_PREMULTIPLIED),
                                             96, 96),
                      display.put()),
                  "Cannot display the screenshot.");
        rt->PushAxisAlignedClip({canvas.left, canvas.top, canvas.right, canvas.bottom},
                                D2D1_ANTIALIAS_MODE_ALIASED);
        auto o = app.view.origin;
        float w = app.image.width * app.view.scale, h = app.image.height * app.view.scale;
        rounded({o.x - 5, o.y + 5, o.x + w + 5, o.y + h + 11}, rgb(231, 233, 242), 8);
        rounded({o.x - 2, o.y + 2, o.x + w + 2, o.y + h + 5}, rgb(218, 222, 234), 5);
        rt->SetTransform(D2D1::Matrix3x2F::Scale(app.view.scale, app.view.scale) *
                         D2D1::Matrix3x2F::Translation(o.x, o.y));
        rt->PushAxisAlignedClip(D2D1::RectF(0, 0, static_cast<float>(app.image.width),
                                            static_cast<float>(app.image.height)),
                                D2D1_ANTIALIAS_MODE_ALIASED);
        rt->DrawBitmap(display.get(),
                       D2D1::RectF(0, 0, static_cast<float>(app.image.width),
                                   static_cast<float>(app.image.height)),
                       1,
                       app.view.scale * app.dpi >= 1
                           ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                           : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        app.graphics.drawAnnotations(rt, app.document.items);
        rt->PopAxisAlignedClip();
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        if (selected() && app.drag != Drag::Draw)
        {
            const auto &item = app.document.items[app.document.selected];
            auto box = item.bounds();
            auto a = app.view.toScreen({box.left, box.top}),
                 b = app.view.toScreen({box.right, box.bottom});
            brush->SetColor(color(Accent, .7f));
            if (item.kind != Tool::Arrow && item.kind != Tool::Line)
                rt->DrawRectangle({a.x, a.y, b.x, b.y}, brush.get(), 1);
            for (auto p : handles(item))
            {
                p = app.view.toScreen(p);
                brush->SetColor(color(rgb(255, 255, 255)));
                rt->FillEllipse(D2D1::Ellipse({p.x, p.y}, 4, 4), brush.get());
                brush->SetColor(color(Accent));
                rt->DrawEllipse(D2D1::Ellipse({p.x, p.y}, 4, 4), brush.get(), 1.5f);
            }
        }
        rt->PopAxisAlignedClip();
    }
    else
    {
        float middle = canvas.top + canvas.height() / 2;
        float cx = client.right / 2;
        // The compact layout keeps the primary action usable in short editor windows.
        if (canvas.height() > 290)
        {
            rounded({cx - 32, middle - 133, cx + 32, middle - 69}, rgb(236, 230, 255), 20);
            drawUIIcon(rt, brush.get(), NewSnip, {cx - 10, middle - 111}, Accent);
            brush->SetColor(color(rgb(244, 182, 46)));
            rt->DrawLine({cx + 38, middle - 129}, {cx + 38, middle - 117}, brush.get(), 2,
                         app.graphics.roundStroke.get());
            rt->DrawLine({cx + 32, middle - 123}, {cx + 44, middle - 123}, brush.get(), 2,
                         app.graphics.roundStroke.get());
        }
        text(L"Small snip. Big possibilities.", {0, middle - 57, client.right, middle - 13}, Ink,
             app.graphics.titleFont.get(), true);
        text(L"Capture a moment. Add your own little touch.",
             {0, middle - 7, client.right, middle + 19}, Muted, app.graphics.font.get(), true);
        // Primary action was painted with the toolbar buttons above.
        if (canvas.height() > 230)
        {
            text(LOBYTE(app.hotkey) ? L"or press " + hotkeyName(app.hotkey)
                                    : L"Drag to capture an area",
                 {0, middle + 80, client.right, middle + 106}, Muted, app.graphics.smallFont.get(),
                 true);
            if (canvas.height() > 350)
                text(L"1  Capture     \u00B7     2  Make your mark     \u00B7     3  Copy & share",
                     {0, middle + 138, client.right, middle + 164}, Muted,
                     app.graphics.smallFont.get(), true);
        }
    }
    fill({0, client.bottom - StatusHeight, client.right, client.bottom}, rgb(255, 255, 255));
    fill({0, client.bottom - StatusHeight, client.right, client.bottom - StatusHeight + 1},
         rgb(231, 233, 241));
    std::wstring message = app.status;
    if (message.empty())
    {
        if (hasImage())
        {
            const wchar_t *hints[] = {
                L"Select: drag to move; corner handles resize; Delete removes",
                L"Pen: drag to draw; Ctrl+Z undoes",
                L"Circle: drag to draw; Shift makes a circle",
                L"Arrow: drag to draw; select and drag endpoints to turn",
                L"Check: click to place; drag to size",
                L"Line: drag to draw; Shift snaps angle; drag endpoints to resize"};
            message = hints[static_cast<int>(app.tool)];
        }
        else
            message = L"Ready when you are";
    }
    brush->SetColor(color(app.status.empty() ? Accent : rgb(42, 169, 106)));
    rt->FillEllipse(D2D1::Ellipse({22, client.bottom - StatusHeight / 2}, 3, 3), brush.get());
    text(message,
         {34, client.bottom - StatusHeight, client.right - (hasImage() ? 210 : 14), client.bottom},
         Muted, app.graphics.smallFont.get());
    if (hasImage())
        text(std::to_wstring(static_cast<int>(std::round(app.view.scale * app.dpi * 100))) +
                 L"%   \u00B7   " + std::to_wstring(app.image.width) + L" \u00D7 " +
                 std::to_wstring(app.image.height),
             {client.right - 190, client.bottom - StatusHeight, client.right - 12, client.bottom},
             Muted, app.graphics.smallFont.get(), true);
    HRESULT result = rt->EndDraw();
    if (result == D2DERR_RECREATE_TARGET)
    {
        app.displayBitmap.reset();
        app.target.reset();
        repaint();
    }
    else
        check(result, "Cannot draw the editor.");
}
Bitmap renderEditorPreview()
{
    // Render the exact editor drawing routine offscreen, independent of foreground-window
    // ownership.
    app.graphics.initialize();
    RECT rect{};
    GetClientRect(app.window, &rect);
    auto result = Bitmap::create(rect.right, rect.bottom);
    Com<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           __uuidof(IWICImagingFactory), reinterpret_cast<void **>(factory.put())),
          "Cannot initialize editor preview.");
    Com<IWICBitmap> bitmap;
    check(factory->CreateBitmap(result.width, result.height, GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapCacheOnLoad, bitmap.put()),
          "Cannot create editor preview.");
    Com<ID2D1RenderTarget> target;
    check(app.graphics.factory->CreateWicBitmapRenderTarget(
              bitmap.get(),
              D2D1::RenderTargetProperties(
                  D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                  app.dpi * 96, app.dpi * 96),
              target.put()),
          "Cannot render editor preview.");
    paintEditor(target.get());
    check(bitmap->CopyPixels(nullptr, result.width * 4, static_cast<UINT>(result.pixels.size()),
                             result.pixels.data()),
          "Cannot read editor preview.");
    return result;
}
void changeColor(Color value)
{
    // Recoloring a selected shape updates that shape tool's preference, even in Select mode.
    const size_t toolIndex = static_cast<size_t>(selected()
        ? app.document.items[app.document.selected].kind : app.tool);
    if (app.colors[toolIndex] != value)
    {
        app.colors[toolIndex] = value;
        app.toolPreferencesDirty = true;
    }
    if (selected() && app.document.items[app.document.selected].color != value)
    {
        app.document.begin();
        app.document.items[app.document.selected].color = value;
        app.document.commit();
        app.dirty = true;
        updateTitle();
    }
    repaint();
}
void customColor()
{
    static COLORREF custom[16]{};
    CHOOSECOLORW chooser{};
    chooser.lStructSize = sizeof(chooser);
    chooser.hwndOwner = app.window;
    chooser.rgbResult = activeColor();
    chooser.lpCustColors = custom;
    chooser.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&chooser))
        changeColor(chooser.rgbResult);
}
void changeThickness(int delta)
{
    float previous = selected() && app.document.items[app.document.selected].kind != Tool::Check
                         ? app.document.items[app.document.selected].thickness
                         : app.thickness;
    float value = std::clamp(previous + delta, 1.0f, 40.0f);
    app.thickness = value;
    if (value != previous && selected() &&
        app.document.items[app.document.selected].kind != Tool::Check)
    {
        app.document.begin();
        app.document.items[app.document.selected].thickness = value;
        app.document.commit();
        app.dirty = true;
        updateTitle();
    }
    repaint();
}
void selectTool(Tool tool)
{
    app.tool = tool;
    app.document.selected = -1;
    repaint();
}
void finishDrag(bool cancel = false)
{
    if (app.drag == Drag::None)
        return;
    if (app.document.editing())
    {
        if (cancel || !app.changed)
        {
            const int selection = app.document.selected;
            const bool keepSelection = app.drag != Drag::Draw && selection >= 0 &&
                                       static_cast<size_t>(selection) < app.document.items.size();
            app.document.cancel();
            if (keepSelection && static_cast<size_t>(selection) < app.document.items.size())
                app.document.selected = selection;
        }
        else
        {
            app.document.commit();
            app.dirty = true;
            updateTitle();
        }
    }
    app.drag = Drag::None;
    app.changed = false;
    if (GetCapture() == app.window)
        ReleaseCapture();
    repaint();
}
void mouseDown(LPARAM lp, bool middle = false)
{
    SetFocus(app.window);
    Point screen{GET_X_LPARAM(lp) / app.dpi, GET_Y_LPARAM(lp) / app.dpi};
    if (!middle)
    {
        for (size_t i = 0; i < app.buttons.size(); ++i)
        {
            const auto &button = app.buttons[i];
            if (button.rect.contains(screen))
            {
                if (enabled(button.command))
                {
                    app.pressed = static_cast<int>(i + 1);
                    app.hover = button.command;
                    SetCapture(app.window);
                    repaint();
                }
                return;
            }
        }
    }
    if (!hasImage() || !canvasRect().contains(screen))
        return;
    if (middle || app.spaceDown)
    {
        app.drag = Drag::Pan;
        app.dragStart = screen;
        app.panStart = app.view.origin;
        app.fit = false;
        SetCapture(app.window);
        return;
    }
    Point p = app.view.toImage(screen);
    app.dragStart = p;
    app.changed = false;
    if (app.tool == Tool::Select)
    {
        if (selected())
        {
            auto points = handles(app.document.items[app.document.selected]);
            for (size_t i = 0; i < points.size(); ++i)
                if (length(screen - app.view.toScreen(points[i])) <= 9)
                {
                    app.handle = static_cast<int>(i);
                    app.before = app.document.items[app.document.selected];
                    app.document.begin();
                    app.drag = app.before.kind == Tool::Arrow || app.before.kind == Tool::Line
                                   ? Drag::Endpoint
                                   : Drag::Resize;
                    SetCapture(app.window);
                    return;
                }
        }
        app.document.selected = app.document.hit(p, 6 / app.view.scale);
        if (selected())
        {
            app.before = app.document.items[app.document.selected];
            app.document.begin();
            app.drag = Drag::Move;
            SetCapture(app.window);
        }
        repaint();
        return;
    }
    if (!Rect{0, 0, static_cast<float>(app.image.width), static_cast<float>(app.image.height)}
             .contains(p))
        return;
    app.document.begin();
    Annotation item;
    item.kind = app.tool;
    item.color = app.colors[static_cast<size_t>(app.tool)];
    item.thickness = app.thickness;
    item.style = app.styles[static_cast<size_t>(app.tool)];
    item.a = item.b = p;
    if (app.tool == Tool::Pen)
        item.points.push_back(p);
    app.document.items.push_back(std::move(item));
    app.document.selected = static_cast<int>(app.document.items.size()) - 1;
    app.drag = Drag::Draw;
    app.changed = true;
    SetCapture(app.window);
    repaint();
}
void mouseMove(LPARAM lp)
{
    Point screen{GET_X_LPARAM(lp) / app.dpi, GET_Y_LPARAM(lp) / app.dpi};
    if (app.drag == Drag::None)
    {
        int hover = 0;
        for (const auto &button : app.buttons)
            if (button.rect.contains(screen))
            {
                hover = button.command;
                break;
            }
        if (hover != app.hover)
        {
            app.hover = hover;
            repaint();
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, app.window, 0};
        TrackMouseEvent(&track);
        return;
    }
    if (app.drag == Drag::Pan)
    {
        app.view.origin = app.panStart + (screen - app.dragStart);
        repaint();
        return;
    }
    if (!selected())
        return;
    Point p = app.view.toImage(screen);
    auto &item = app.document.items[app.document.selected];
    if (app.drag == Drag::Draw)
    {
        p = limited(p);
        if (app.tool == Tool::Pen)
        {
            if (length(p - item.points.back()) >= .6f)
                item.points.push_back(p);
        }
        else if (app.tool == Tool::Line && (GetKeyState(VK_SHIFT) & 0x8000))
        {
            Point delta = p - app.dragStart;
            constexpr float step = 3.14159265f / 4;
            float angle = std::round(std::atan2(delta.y, delta.x) / step) * step;
            item.b =
                limited(app.dragStart + Point{std::cos(angle), std::sin(angle)} * length(delta));
        }
        else if (app.tool == Tool::Circle && (GetKeyState(VK_SHIFT) & 0x8000))
        {
            Point delta = p - app.dragStart;
            float side = std::min(std::abs(delta.x), std::abs(delta.y));
            item.b = app.dragStart + Point{delta.x < 0 ? -side : side, delta.y < 0 ? -side : side};
        }
        else if (app.tool == Tool::Check)
        {
            Point delta = p - app.dragStart;
            float side = std::min(std::abs(delta.x), std::abs(delta.y));
            item.b = app.dragStart + Point{delta.x < 0 ? -side : side, delta.y < 0 ? -side : side};
        }
        else
            item.b = p;
    }
    else
    {
        item = app.before;
        if (app.drag == Drag::Move)
            item.move(p - app.dragStart);
        else if (app.drag == Drag::Endpoint)
        {
            if (app.handle == 0)
                item.a = p;
            else
                item.b = p;
        }
        else if (app.drag == Drag::Resize)
        {
            auto corners = handles(app.before);
            Point opposite = corners[(app.handle + 2) % 4];
            if (item.kind == Tool::Check || (GetKeyState(VK_SHIFT) & 0x8000))
            {
                auto r = app.before.bounds();
                Point delta = p - opposite;
                float aspect = r.height() > .001f ? r.width() / r.height() : 1;
                float w = std::max(4.0f, std::abs(delta.x)), h = w / std::max(.01f, aspect);
                p = {opposite.x + (delta.x < 0 ? -w : w), opposite.y + (delta.y < 0 ? -h : h)};
            }
            auto to = rectangle(opposite, p);
            if (to.width() >= 2 && to.height() >= 2)
                item.resize(app.before.bounds(), to);
        }
        if (length(p - app.dragStart) > .01f)
            app.changed = true;
    }
    repaint();
}
void mouseUp(LPARAM lp)
{
    if (app.pressed)
    {
        const size_t index = static_cast<size_t>(app.pressed - 1);
        app.pressed = 0;
        ReleaseCapture();
        Point screen{GET_X_LPARAM(lp) / app.dpi, GET_Y_LPARAM(lp) / app.dpi};
        if (index < app.buttons.size() && app.buttons[index].rect.contains(screen) &&
            enabled(app.buttons[index].command))
            command(app.buttons[index].command);
        repaint();
        return;
    }
    if (app.drag == Drag::Draw && selected())
    {
        auto &item = app.document.items[app.document.selected];
        if (item.kind != Tool::Pen && length(item.b - item.a) < 4)
        {
            float size = item.kind == Tool::Check ? 56 : 90;
            size = std::min(
                {size, static_cast<float>(app.image.width), static_cast<float>(app.image.height)});
            Point a{std::clamp(item.a.x - size / 2, 0.0f, app.image.width - size),
                    std::clamp(item.a.y - size / 2, 0.0f, app.image.height - size)};
            if (item.kind == Tool::Line)
                a.y = item.a.y;
            item.a = a;
            item.b = a + Point{size, item.kind == Tool::Line ? 0.0f : size};
        }
        // Shape tools switch to selection after placement so moving the new object takes one drag.
        if (item.kind != Tool::Pen)
            app.tool = Tool::Select;
        else
            app.document.selected = -1;
    }
    finishDrag();
}
void zoomAt(Point screen, float factor)
{
    if (!hasImage())
        return;
    Point point = app.view.toImage(screen);
    app.fit = false;
    app.view.scale = std::clamp(app.view.scale * factor, .01f / app.dpi, 8 / app.dpi);
    app.view.origin = screen - point * app.view.scale;
    repaint();
}
void copyImage()
{
    if (!hasImage())
        return;
    auto bitmap = app.graphics.flatten(app.image, app.document.items);
    auto png = app.graphics.png(bitmap);
    if (!copyBitmap(app.window, bitmap, png))
    {
        status(L"Clipboard is busy. Try Ctrl+C again.");
        return;
    }
    app.dirty = false;
    updateTitle();
    status(L"Copied image and annotations - ready to paste");
}
bool chooseSave(std::wstring &path)
{
    wchar_t buffer[32768]{};
    if (!path.empty())
        wcsncpy_s(buffer, path.c_str(), _TRUNCATE);
    else
    {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        swprintf_s(buffer, L"Snip-%04u%02u%02u-%02u%02u%02u.png", time.wYear, time.wMonth,
                   time.wDay, time.wHour, time.wMinute, time.wSecond);
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = IsWindowVisible(app.window) ? app.window : nullptr;
    dialog.lpstrFilter = L"PNG image (*.png)\0*.png\0\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = 32768;
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog))
    {
        if (CommDlgExtendedError())
            throw std::runtime_error("Windows could not open the save dialog.");
        return false;
    }
    path = buffer;
    auto dot = path.find_last_of(L'.'), slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        path += L".png";
    else if (_wcsicmp(path.substr(dot).c_str(), L".png") != 0)
    {
        MessageBoxW(IsWindowVisible(app.window) ? app.window : nullptr,
                    L"Snipper saves PNG images. Use a filename ending in .png.", L"Save as PNG",
                    MB_OK | MB_ICONINFORMATION);
        return chooseSave(path);
    }
    return true;
}
void saveImage(bool saveAs = false)
{
    if (!hasImage())
        return;
    std::wstring path = app.savePath;
    if ((path.empty() || saveAs) && !chooseSave(path))
        return;
    auto bitmap = app.graphics.flatten(app.image, app.document.items);
    saveBytes(path, app.graphics.png(bitmap));
    app.savePath = path;
    app.dirty = false;
    updateTitle();
    status(L"Saved PNG with all annotations");
}
void closeSettings()
{
    if (app.settingsWindow)
    {
        HWND window = app.settingsWindow;
        app.settingsWindow = nullptr;
        app.hotkeyControl = nullptr;
        EnableWindow(app.window, TRUE);
        DestroyWindow(window);
        SetForegroundWindow(app.window);
    }
}
void openSettings()
{
    if (app.settingsWindow)
    {
        SetForegroundWindow(app.settingsWindow);
        return;
    }
    showEditor();
    float d = app.dpi;
    RECT owner{};
    GetWindowRect(app.window, &owner);
    int width = static_cast<int>(460 * d), height = static_cast<int>(230 * d);
    app.settingsWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, SettingsClass, L"Keyboard shortcut",
                                         WS_CAPTION | WS_SYSMENU,
                                         owner.left + (owner.right - owner.left - width) / 2,
                                         owner.top + (owner.bottom - owner.top - height) / 2, width,
                                         height, app.window, nullptr, app.instance, nullptr);
    if (!app.settingsWindow)
        throw std::runtime_error("Cannot open shortcut settings.");
    EnableWindow(app.window, FALSE);
    ShowWindow(app.settingsWindow, SW_SHOW);
    SetFocus(app.hotkeyControl);
}
void trayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, NewSnip, L"Snip now");
    AppendMenuW(menu, MF_STRING, ShowEditor, L"Open editor");
    AppendMenuW(menu, MF_STRING, Settings, L"Keyboard shortcut...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Exit, L"Exit Snipper");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(app.window);
    int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, app.window,
                            nullptr);
    DestroyMenu(menu);
    PostMessageW(app.window, WM_NULL, 0, 0);
    if (id)
        command(id);
}
void command(int id)
{
    if (app.drag != Drag::None)
        finishDrag();
    if (id >= ColorFirst && id < ColorFirst + 8)
    {
        changeColor(Palette[id - ColorFirst]);
        return;
    }
    if (id >= SelectTool && id <= LineTool)
    {
        if (hasImage())
            selectTool(static_cast<Tool>(id - SelectTool));
        return;
    }
    if (id >= CircleStyleMenu && id <= LineStyleMenu)
    {
        if (!hasImage())
            return;
        const int toolOffset = id - CircleStyleMenu;
        const Tool tool = static_cast<Tool>(static_cast<int>(Tool::Circle) + toolOffset);
        const wchar_t *const labels[][4] = {
            {L"Outline", L"Soft highlight", L"Dashed outline", nullptr},
            {L"Classic", L"Outlined", L"Curved gloss", L"Straight gloss"},
            {L"Boxed", L"Circle badge", L"Simple check", nullptr},
            {L"Solid", L"Dashed", L"Dotted", nullptr}};
        HMENU menu = CreatePopupMenu();
        if (!menu)
            return;
        const size_t toolIndex = static_cast<size_t>(tool);
        const int base = styleCommand(tool, 0);
        for (int style = 0; style < StyleCounts[toolIndex]; ++style)
            AppendMenuW(menu, MF_STRING | (app.styles[toolIndex] == style ? MF_CHECKED : 0),
                        base + style, labels[toolOffset][style]);
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(app.window);
        const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y,
                                          0, app.window, nullptr);
        DestroyMenu(menu);
        PostMessageW(app.window, WM_NULL, 0, 0);
        if (choice)
            command(choice);
        return;
    }
    if (id >= StyleChoiceFirst && id < StyleChoiceFirst + 16)
    {
        const int option = id - StyleChoiceFirst;
        const Tool tool = static_cast<Tool>(static_cast<int>(Tool::Circle) + option / 4);
        const size_t toolIndex = static_cast<size_t>(tool);
        const auto style = static_cast<uint8_t>(option % 4);
        if (!hasImage() || style >= StyleCounts[toolIndex])
            return;
        if (app.styles[toolIndex] != style)
        {
            app.styles[toolIndex] = style;
            app.toolPreferencesDirty = true;
        }
        selectTool(tool);
        return;
    }
    switch (id)
    {
    case NewSnip:
        startSnip();
        break;
    case Copy:
        copyImage();
        break;
    case Save:
        saveImage();
        break;
    case SaveAs:
        saveImage(true);
        break;
    case Undo:
        if (app.document.undo())
        {
            app.dirty = true;
            updateTitle();
            repaint();
        }
        break;
    case Redo:
        if (app.document.redo())
        {
            app.dirty = true;
            updateTitle();
            repaint();
        }
        break;
    case DeleteSelected:
        if (selected())
        {
            app.document.begin();
            app.document.items.erase(app.document.items.begin() + app.document.selected);
            app.document.selected = -1;
            app.document.commit();
            app.dirty = true;
            updateTitle();
            repaint();
        }
        break;
    case Clear:
        if (!app.document.items.empty())
        {
            app.document.begin();
            app.document.items.clear();
            app.document.selected = -1;
            app.document.commit();
            app.dirty = true;
            updateTitle();
            repaint();
        }
        break;
    case Fit:
        app.fit = true;
        updateView();
        repaint();
        break;
    case Actual:
        if (hasImage())
        {
            auto r = canvasRect();
            app.fit = false;
            app.view.scale = 1 / app.dpi;
            app.view.origin = {(r.width() - app.image.width * app.view.scale) / 2,
                               r.top + (r.height() - app.image.height * app.view.scale) / 2};
            repaint();
        }
        break;
    case CustomColor:
        customColor();
        break;
    case SizeDown:
        changeThickness(-1);
        break;
    case SizeUp:
        changeThickness(1);
        break;
    case Settings:
        openSettings();
        break;
    case Startup:
        toggleStartup();
        break;
    case ShowEditor:
        showEditor();
        break;
    case About:
        MessageBoxW(
            app.window,
            L"Snipper 1.0.1\n\nNative C++ screenshot editor.\n\nCtrl+N: new snip\nCtrl+C: "
            L"copy image with annotations\nCtrl+S: save PNG\nCtrl+Shift+S: Save As\nCtrl+Z "
            L"/ Ctrl+Y: undo / redo\nV / P / O / A / K / L: select / pen / circle / arrow / "
            L"check / line\n[ / ]: brush size\nDelete: remove selection\nCtrl+wheel: "
            L"zoom\nMiddle-drag or Space+drag: pan\nEsc: cancel capture or current "
            L"edit\n\nClose the window to stay in the tray.\nFile > Exit quits "
            L"completely.\n\nShortcut settings are saved beside the executable.",
            L"About Snipper", MB_OK | MB_ICONINFORMATION);
        break;
    case Exit:
        if (canDiscard())
        {
            saveToolPreferencesOrNotify();
            app.exiting = true;
            DestroyWindow(app.window);
        }
        break;
    default:
        break;
    }
}
void cancelCapture(bool restore = true)
{
    if (app.window)
        KillTimer(app.window, CaptureTimer);
    if (app.overlay)
    {
        HWND overlay = app.overlay;
        app.overlay = nullptr;
        DestroyWindow(overlay);
    }
    if (app.overlayDC && app.overlayPrevious)
        SelectObject(app.overlayDC, app.overlayPrevious);
    if (app.overlaySurface)
        DeleteObject(app.overlaySurface);
    if (app.overlayDC)
        DeleteDC(app.overlayDC);
    app.overlayDC = nullptr;
    app.overlaySurface = nullptr;
    app.overlayPrevious = nullptr;
    app.desktop = {};
    app.dimDesktop = {};
    app.selecting = false;
    app.capturePending = false;
    if (restore)
        showEditor();
}
void cloakEditorForCapture()
{
    // Cloaking removes the editor's pixels without changing Win32 visibility/focus.
    // For hotkeys, defer SW_HIDE and all dialogs until the desktop has been frozen.
    if (!IsWindowVisible(app.window))
        return;
    const BOOL disabled = TRUE;
    DwmSetWindowAttribute(app.window, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof(disabled));
    check(DwmSetWindowAttribute(app.window, DWMWA_CLOAK, &disabled, sizeof(disabled)),
          "Cannot remove the editor from capture.");
}
void hideEditorForCapture()
{
    cloakEditorForCapture();
    ShowWindow(app.window, SW_HIDE);
}
void freezeDesktop()
{
    app.virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    app.virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    DwmFlush();
    app.desktop = captureDesktop(app.virtualX, app.virtualY, width, height);
}
void startSnip(bool instant)
{
    if (app.overlay || app.capturePending || app.settingsWindow)
        return;
    // Modal dialogs pump hotkeys too. Reserve the request before opening one,
    // so another rapid snip cannot start capture with an earlier dialog still open.
    app.capturePending = true;
    try
    {
        if (instant)
        {
            cloakEditorForCapture();
            freezeDesktop();
            // Protect the previous edited image, but only ask after transient UI is captured.
            if (app.dirty)
                showEditor();
        }
        if (!canDiscard(true))
        {
            cancelCapture();
            return;
        }
        hideEditorForCapture();
        if (instant)
            openOverlay();
        else if (!SetTimer(app.window, CaptureTimer, 65, nullptr))
            throw std::runtime_error("Cannot start screen capture.");
    }
    catch (...)
    {
        cancelCapture();
        throw;
    }
}
void openOverlay()
{
    hideEditorForCapture();
    if (IsWindowVisible(app.window))
        throw std::runtime_error("The editor could not be hidden before capture.");
    if (app.desktop.empty())
        freezeDesktop();
    const int width = app.desktop.width, height = app.desktop.height;
    app.dimDesktop = app.desktop;
    for (size_t i = 0; i < app.dimDesktop.pixels.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            app.dimDesktop.pixels[i + c] =
                static_cast<uint8_t>(app.dimDesktop.pixels[i + c] * .48f);
    app.selecting = false;
    app.overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, OverlayClass,
                                  L"Snipper selection", WS_POPUP, app.virtualX, app.virtualY, width,
                                  height, nullptr, nullptr, app.instance, nullptr);
    if (!app.overlay)
        throw std::runtime_error("Cannot open the selection overlay.");
    HDC dc = GetDC(app.overlay);
    app.overlayDC = CreateCompatibleDC(dc);
    app.overlaySurface = CreateCompatibleBitmap(dc, width, height);
    ReleaseDC(app.overlay, dc);
    if (!app.overlayDC || !app.overlaySurface)
        throw std::runtime_error("Cannot allocate the capture overlay.");
    app.overlayPrevious = SelectObject(app.overlayDC, app.overlaySurface);
    SetWindowPos(app.overlay, HWND_TOPMOST, app.virtualX, app.virtualY, width, height,
                 SWP_SHOWWINDOW);
    SetForegroundWindow(app.overlay);
    SetFocus(app.overlay);
    app.capturePending = false;
}
POINT overlayPoint()
{
    POINT point{};
    GetCursorPos(&point);
    point.x = std::clamp(point.x - app.virtualX, 0L, static_cast<LONG>(app.desktop.width));
    point.y = std::clamp(point.y - app.virtualY, 0L, static_cast<LONG>(app.desktop.height));
    return point;
}
POINT selectionEventPoint(LPARAM lp)
{
    return {
        std::clamp(static_cast<LONG>(GET_X_LPARAM(lp)), 0L, static_cast<LONG>(app.desktop.width)),
        std::clamp(static_cast<LONG>(GET_Y_LPARAM(lp)), 0L, static_cast<LONG>(app.desktop.height))};
}
RECT selectionRect()
{
    return {std::min(app.selectionStart.x, app.selectionEnd.x),
            std::min(app.selectionStart.y, app.selectionEnd.y),
            std::max(app.selectionStart.x, app.selectionEnd.x),
            std::max(app.selectionStart.y, app.selectionEnd.y)};
}
void finishCapture()
{
    auto rect = selectionRect();
    if (rect.right - rect.left < 2 || rect.bottom - rect.top < 2)
    {
        app.selecting = false;
        InvalidateRect(app.overlay, nullptr, FALSE);
        return;
    }
    Bitmap captured =
        app.desktop.crop(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    cancelCapture(false);
    releaseImage();
    app.image = std::move(captured);
    app.tool = Tool::Pen;
    app.fit = true;
    app.status.clear();
    updateTitle();
    showEditor();
}
void paintOverlay(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (app.desktop.empty() || !app.overlayDC)
    {
        EndPaint(hwnd, &paint);
        return;
    }
    HDC memory = app.overlayDC;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = app.desktop.width;
    info.bmiHeader.biHeight = -app.desktop.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(memory, 0, 0, app.desktop.width, app.desktop.height, 0, 0, 0,
                      app.desktop.height, app.dimDesktop.pixels.data(), &info, DIB_RGB_COLORS);
    if (app.selecting)
    {
        auto r = selectionRect();
        int width = r.right - r.left, height = r.bottom - r.top;
        if (width > 0 && height > 0)
        {
            int saved = SaveDC(memory);
            IntersectClipRect(memory, r.left, r.top, r.right, r.bottom);
            SetDIBitsToDevice(memory, 0, 0, app.desktop.width, app.desktop.height, 0, 0, 0,
                              app.desktop.height, app.desktop.pixels.data(), &info, DIB_RGB_COLORS);
            RestoreDC(memory, saved);
        }
        HBRUSH clear = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(96, 165, 250));
        auto oldPen = SelectObject(memory, pen), oldBrush = SelectObject(memory, clear);
        Rectangle(memory, r.left, r.top, r.right, r.bottom);
        SelectObject(memory, oldPen);
        SelectObject(memory, oldBrush);
        DeleteObject(pen);
        wchar_t label[100]{};
        swprintf_s(label, L"%ld x %ld   |   Esc to cancel", static_cast<long>(width),
                   static_cast<long>(height));
        int lx = std::clamp(static_cast<int>(r.left), 8, std::max(8, app.desktop.width - 300)),
            ly = r.top >= 35 ? static_cast<int>(r.top) - 32
                             : std::min(static_cast<int>(r.bottom) + 8, app.desktop.height - 30);
        RECT box{lx, ly, lx + 280, ly + 26};
        HBRUSH bg = CreateSolidBrush(RGB(15, 23, 42));
        FillRect(memory, &box, bg);
        DeleteObject(bg);
        SetBkMode(memory, TRANSPARENT);
        SetTextColor(memory, RGB(255, 255, 255));
        SelectObject(memory, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(memory, label, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
        POINT cursor = overlayPoint();
        int x = std::clamp(static_cast<int>(cursor.x) + 20, 8,
                           std::max(8, app.desktop.width - 340)),
            y = std::clamp(static_cast<int>(cursor.y) + 24, 8,
                           std::max(8, app.desktop.height - 44));
        RECT box{x, y, x + 326, y + 34};
        HBRUSH bg = CreateSolidBrush(RGB(15, 23, 42));
        FillRect(memory, &box, bg);
        DeleteObject(bg);
        SetBkMode(memory, TRANSPARENT);
        SetTextColor(memory, RGB(255, 255, 255));
        SelectObject(memory, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(memory, L"Drag to select an area   |   Esc to cancel", -1, &box,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right - paint.rcPaint.left,
           paint.rcPaint.bottom - paint.rcPaint.top, memory, paint.rcPaint.left, paint.rcPaint.top,
           SRCCOPY);
    EndPaint(hwnd, &paint);
}
HMENU createMenu()
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(),
          view = CreatePopupMenu(), settings = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, NewSnip, L"&New snip\tCtrl+N");
    AppendMenuW(file, MF_STRING, Copy, L"&Copy image\tCtrl+C");
    AppendMenuW(file, MF_STRING, Save, L"&Save PNG\tCtrl+S");
    AppendMenuW(file, MF_STRING, SaveAs, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, Exit, L"E&xit");
    AppendMenuW(edit, MF_STRING, Undo, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, Redo, L"&Redo\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, DeleteSelected, L"&Delete selection\tDel");
    AppendMenuW(edit, MF_STRING, Clear, L"Clear &annotations");
    AppendMenuW(view, MF_STRING, Fit, L"&Fit image");
    AppendMenuW(view, MF_STRING, Actual, L"&Actual size (100%)");
    AppendMenuW(settings, MF_STRING, Settings, L"&Keyboard shortcut...");
    AppendMenuW(settings, MF_STRING, Startup, L"Run at &sign-in");
    AppendMenuW(help, MF_STRING, About, L"&About and shortcuts");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), L"&Settings");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    return bar;
}
void updateMenus()
{
    HMENU menu = GetMenu(app.window);
    auto enable = [&](int id, bool yes) {
        EnableMenuItem(menu, id, MF_BYCOMMAND | (yes ? MF_ENABLED : MF_GRAYED));
    };
    for (int id : {Copy, Save, SaveAs, Fit, Actual})
        enable(id, hasImage());
    enable(Undo, app.document.canUndo());
    enable(Redo, app.document.canRedo());
    enable(DeleteSelected, selected());
    enable(Clear, !app.document.items.empty());
    CheckMenuItem(menu, Startup, MF_BYCOMMAND | (startupEnabled() ? MF_CHECKED : MF_UNCHECKED));
}
void processKey(WPARAM key)
{
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
         shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (key == VK_ESCAPE)
    {
        if (app.pressed)
        {
            app.pressed = 0;
            ReleaseCapture();
            repaint();
        }
        else if (app.drag != Drag::None)
            finishDrag(true);
        else
        {
            app.document.selected = -1;
            app.tool = Tool::Select;
            repaint();
        }
        return;
    }
    if (app.drag != Drag::None)
        return;
    if (ctrl)
    {
        switch (key)
        {
        case 'N':
            command(NewSnip);
            break;
        case 'C':
            command(Copy);
            break;
        case 'S':
            command(shift ? SaveAs : Save);
            break;
        case 'Z':
            command(shift ? Redo : Undo);
            break;
        case 'Y':
            command(Redo);
            break;
        default:
            break;
        }
        return;
    }
    switch (key)
    {
    case VK_DELETE:
        command(DeleteSelected);
        break;
    case 'V':
        command(SelectTool);
        break;
    case 'P':
        command(PenTool);
        break;
    case 'O':
        command(CircleTool);
        break;
    case 'A':
        command(ArrowTool);
        break;
    case 'K':
        command(CheckTool);
        break;
    case 'L':
        command(LineTool);
        break;
    case VK_OEM_4:
        command(SizeDown);
        break;
    case VK_OEM_6:
        command(SizeUp);
        break;
    case VK_SPACE:
        app.spaceDown = true;
        break;
    default:
        break;
    }
}
LRESULT mainMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == app.taskbarCreated)
    {
        app.tray = false;
        addTray();
        return 0;
    }
    switch (message)
    {
    case WM_CREATE:
        app.window = hwnd;
        app.dpi = dpiFor(hwnd);
        app.tooltip =
            CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, app.instance, nullptr);
        if (app.tooltip)
        {
            SendMessageW(app.tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 550);
            SendMessageW(app.tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
        }
        addTray();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT p{};
        BeginPaint(hwnd, &p);
        try
        {
            paintEditor();
        }
        catch (...)
        {
            EndPaint(hwnd, &p);
            throw;
        }
        EndPaint(hwnd, &p);
        return 0;
    }
    case WM_SIZE:
        if (app.target && wp != SIZE_MINIMIZED)
        {
            auto size = D2D1::SizeU(LOWORD(lp), HIWORD(lp));
            app.target->Resize(size);
        }
        updateView();
        repaint();
        return 0;
    case WM_DPICHANGED: {
        app.dpi = HIWORD(wp) / 96.0f;
        if (app.target)
            app.target->SetDpi(app.dpi * 96, app.dpi * 96);
        auto rect = reinterpret_cast<RECT *>(lp);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        repaint();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(lp);
        float d = dpiFor(hwnd);
        info->ptMinTrackSize = {static_cast<LONG>(850 * d), static_cast<LONG>(430 * d)};
        return 0;
    }
    case WM_COMMAND:
        command(LOWORD(wp));
        return 0;
    case WM_INITMENUPOPUP:
        updateMenus();
        return 0;
    case WM_LBUTTONDOWN:
        mouseDown(lp);
        return 0;
    case WM_MBUTTONDOWN:
        mouseDown(lp, true);
        return 0;
    case WM_MOUSEMOVE:
        mouseMove(lp);
        return 0;
    case WM_MOUSELEAVE:
        app.hover = 0;
        repaint();
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
        mouseUp(lp);
        return 0;
    case WM_CAPTURECHANGED:
        if (app.pressed)
        {
            app.pressed = 0;
            repaint();
        }
        if (app.drag != Drag::None)
            finishDrag(true);
        return 0;
    case WM_MOUSEWHEEL: {
        if (!hasImage())
            return 0;
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            zoomAt({p.x / app.dpi, p.y / app.dpi},
                   GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.2f : 1 / 1.2f);
        }
        else
        {
            app.fit = false;
            float amount = GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 48;
            if (GetKeyState(VK_SHIFT) & 0x8000)
                app.view.origin.x += amount;
            else
                app.view.origin.y += amount;
            repaint();
        }
        return 0;
    }
    case WM_KEYDOWN:
        processKey(wp);
        return 0;
    case WM_KEYUP:
        if (wp == VK_SPACE)
            app.spaceDown = false;
        return 0;
    case WM_KILLFOCUS:
        app.spaceDown = false;
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
        {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            Point point{p.x / app.dpi, p.y / app.dpi};
            bool onCanvas = canvasRect().contains(point) && hasImage();
            bool onButton =
                std::any_of(app.buttons.begin(), app.buttons.end(), [&](const Button &b) {
                    return b.rect.contains(point) && enabled(b.command);
                });
            SetCursor(
                LoadCursorW(nullptr, app.drag == Drag::Pan || app.spaceDown
                                         ? IDC_SIZEALL
                                         : (onButton                               ? IDC_HAND
                                            : onCanvas && app.tool != Tool::Select ? IDC_CROSS
                                                                                   : IDC_ARROW)));
            return TRUE;
        }
        break;
    case WM_HOTKEY:
        if (!app.settingsWindow)
            startSnip(true);
        return 0;
    case TrayMessage:
        if (lp == WM_LBUTTONUP)
            startSnip();
        else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)
            trayMenu();
        return 0;
    case LaunchMessage:
        if (wp)
            startSnip();
        else
            showEditor();
        return 0;
    case WM_TIMER:
        if (wp == CaptureTimer)
        {
            KillTimer(hwnd, CaptureTimer);
            try
            {
                if (app.capturePending && !app.overlay)
                    openOverlay();
            }
            catch (...)
            {
                cancelCapture();
                throw;
            }
        }
        else if (wp == StatusTimer)
        {
            KillTimer(hwnd, StatusTimer);
            app.status.clear();
            repaint();
        }
        else if (wp == SmokeTimer)
        {
            KillTimer(hwnd, SmokeTimer);
            app.dirty = false;
            command(Exit);
        }
        return 0;
    case WM_CLOSE:
        if (app.settingsWindow)
        {
            closeSettings();
            return 0;
        }
        if (canDiscard())
        {
            saveToolPreferencesOrNotify();
            if (app.tray)
            {
                releaseImage();
                ShowWindow(hwnd, SW_HIDE);
            }
            else
            {
                app.exiting = true;
                DestroyWindow(hwnd);
            }
        }
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp)
            saveToolPreferences();
        return 0;
    case WM_DESTROY:
        saveToolPreferences();
        cancelCapture(false);
        removeTray();
        if (app.hotkeyRegistered)
            UnregisterHotKey(hwnd, app.hotkeyId);
        if (app.settingsWindow)
            closeSettings();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
LRESULT CALLBACK mainProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    try
    {
        return mainMessage(hwnd, message, wp, lp);
    }
    catch (const std::exception &exception)
    {
        if (message == WM_PAINT)
        {
            ValidateRect(hwnd, nullptr);
            app.displayBitmap.reset();
            app.target.reset();
        }
        error(hwnd, exception.what());
        return 0;
    }
}
LRESULT CALLBACK overlayProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    try
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paintOverlay(hwnd);
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_LBUTTONDOWN:
            app.selectionStart = app.selectionEnd = selectionEventPoint(lp);
            app.selecting = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            if (app.selecting)
                app.selectionEnd = selectionEventPoint(lp);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (app.selecting)
            {
                app.selectionEnd = selectionEventPoint(lp);
                ReleaseCapture();
                finishCapture();
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE)
                cancelCapture();
            return 0;
        case WM_RBUTTONUP:
            cancelCapture();
            return 0;
        case WM_DISPLAYCHANGE:
            cancelCapture();
            return 0;
        case WM_CLOSE:
            cancelCapture();
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
    catch (const std::exception &exception)
    {
        cancelCapture();
        error(app.window, exception.what());
        return 0;
    }
}
LRESULT CALLBACK settingsProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message)
    {
    case WM_CREATE: {
        float d = dpiFor(hwnd);
        auto control = [&](const wchar_t *cls, const wchar_t *label, DWORD style, int x, int y,
                           int w, int h, int id) {
            HWND item = CreateWindowExW(
                0, cls, label, WS_CHILD | WS_VISIBLE | style, static_cast<int>(x * d),
                static_cast<int>(y * d), static_cast<int>(w * d), static_cast<int>(h * d), hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app.instance, nullptr);
            SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(app.dialogFont), TRUE);
            return item;
        };
        control(L"STATIC", L"Press your preferred shortcut:", 0, 20, 15, 390, 24, 0);
        app.hotkeyControl =
            control(HOTKEY_CLASSW, L"", WS_TABSTOP | WS_BORDER, 20, 48, 395, 30, 10);
        SendMessageW(app.hotkeyControl, HKM_SETHOTKEY, app.hotkey, 0);
        control(L"STATIC",
                L"Use Ctrl or Alt. Backspace clears the shortcut.\nThe shortcut starts snipping "
                L"directly while the app runs.",
                0, 20, 89, 395, 40, 0);
        control(L"BUTTON", L"Save", WS_TABSTOP | BS_DEFPUSHBUTTON, 237, 146, 84, 30, IDOK);
        control(L"BUTTON", L"Cancel", WS_TABSTOP, 331, 146, 84, 30, IDCANCEL);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL)
        {
            closeSettings();
            return 0;
        }
        else if (LOWORD(wp) == IDOK)
        {
            WORD value = static_cast<WORD>(SendMessageW(app.hotkeyControl, HKM_GETHOTKEY, 0, 0));
            if (!registerShortcut(value))
                return 0;
            std::wstring number = std::to_wstring(value);
            bool saved = WritePrivateProfileStringW(L"Settings", L"Hotkey", number.c_str(),
                                                    app.iniPath.c_str()) != FALSE;
            closeSettings();
            status(L"Shortcut: " + hotkeyName(app.hotkey));
            if (!saved)
                error(app.window,
                      "Shortcut changed for this session, but settings could not be saved beside "
                      "the executable. Move the app to a writable folder.");
            return 0;
        }
        break;
    case WM_CLOSE:
        closeSettings();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
void registerClasses()
{
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = app.instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(app.instance, MAKEINTRESOURCEW(101));
    cls.hIconSm = cls.hIcon;
    cls.lpszClassName = MainClass;
    cls.lpfnWndProc = mainProcedure;
    if (!RegisterClassExW(&cls))
        throw std::runtime_error("Cannot register the editor window.");
    cls.lpszClassName = OverlayClass;
    cls.lpfnWndProc = overlayProcedure;
    cls.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    if (!RegisterClassExW(&cls))
        throw std::runtime_error("Cannot register capture window.");
    cls.lpszClassName = SettingsClass;
    cls.lpfnWndProc = settingsProcedure;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&cls))
        throw std::runtime_error("Cannot register settings window.");
}
void writeTestReport(const wchar_t *filename, const std::string &content)
{
    std::ofstream file(filename, std::ios::binary);
    file << content;
}
class SmokePromptAction
{
    static inline SmokePromptAction *current = nullptr;
    UINT_PTR timer = 0;
    int decision;
    HWND observed = nullptr;
    bool repeatRequested = false;
    static BOOL CALLBACK findPrompt(HWND window, LPARAM data)
    {
        auto &action = *reinterpret_cast<SmokePromptAction *>(data);
        wchar_t name[80]{}, title[80]{};
        GetClassNameW(window, name, 80);
        GetWindowTextW(window, title, 80);
        if (wcscmp(name, L"#32770") != 0 || wcscmp(title, L"Snipper") != 0 ||
            !IsWindowVisible(window) || !GetDlgItem(window, action.decision))
            return TRUE;
        if (!action.observed)
        {
            action.observed = window;
            action.shown = true;
            DWORD cloaked = 0;
            DwmGetWindowAttribute(app.window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            action.editorVisible = IsWindowVisible(app.window) && !cloaked;
            action.promptOwned = GetWindow(window, GW_OWNER) == app.window;
            action.captureBlocked = app.capturePending;
            GetWindowRect(window, &action.bounds);
        }
        else if (action.observed != window)
            action.extraPrompt = true;
        if (action.captureBlocked && !action.repeatRequested)
        {
            action.repeatRequested = true;
            SendMessageW(app.window, WM_HOTKEY, app.hotkeyId, 0);
            action.repeatIgnored = !action.extraPrompt && app.capturePending && !app.overlay &&
                                   IsWindowVisible(window);
        }
        DwmFlush();
        KillTimer(nullptr, action.timer);
        PostMessageW(window, WM_COMMAND, action.decision, 0);
        return FALSE;
    }
    static void CALLBACK dismiss(HWND, UINT, UINT_PTR, DWORD)
    {
        if (current)
            EnumThreadWindows(GetCurrentThreadId(), findPrompt, reinterpret_cast<LPARAM>(current));
    }

  public:
    bool shown = false, editorVisible = false, promptOwned = false, captureBlocked = false,
         repeatIgnored = false, extraPrompt = false;
    RECT bounds{};
    explicit SmokePromptAction(int response) : decision(response)
    {
        current = this;
        timer = SetTimer(nullptr, 0, 120, dismiss);
        if (!timer)
        {
            current = nullptr;
            throw std::runtime_error("Cannot automate the capture confirmation regression.");
        }
    }
    ~SmokePromptAction()
    {
        KillTimer(nullptr, timer);
        current = nullptr;
    }
};
class SmokeHoverPopup
{
    HWND host = nullptr, popup = nullptr;
    static LRESULT CALLBACK observe(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                    UINT_PTR, DWORD_PTR data)
    {
        auto &fixture = *reinterpret_cast<SmokeHoverPopup *>(data);
        if (message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE &&
            fixture.popup && IsWindowVisible(fixture.popup))
        {
            fixture.dismissed = true;
            ShowWindow(fixture.popup, SW_HIDE);
        }
        return DefSubclassProc(window, message, wp, lp);
    }

  public:
    bool dismissed = false;
    RECT bounds{};
    SmokeHoverPopup(int x, int y)
    {
        constexpr wchar_t className[] = L"JackSnip.SmokeHoverHost.1";
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = app.instance;
        cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        cls.lpszClassName = className;
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Cannot register hover-menu test host.");
        host = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, className, L"Hover menu host",
                              WS_OVERLAPPEDWINDOW, x, y, 320, 200, nullptr, nullptr,
                              app.instance, nullptr);
        if (!host)
            throw std::runtime_error("Cannot create hover-menu test host.");
        if (!SetWindowSubclass(host, observe, 1, reinterpret_cast<DWORD_PTR>(this)))
        {
            DestroyWindow(host);
            throw std::runtime_error("Cannot observe hover-menu focus changes.");
        }
        popup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"Transient hover menu",
                               WS_POPUP | SS_WHITERECT, x + 30, y + 45, 220, 100,
                               host, nullptr, app.instance, nullptr);
        if (!popup)
        {
            DestroyWindow(host);
            throw std::runtime_error("Cannot create hover-menu test popup.");
        }
        const BOOL disabled = TRUE;
        DwmSetWindowAttribute(host, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof(disabled));
        DwmSetWindowAttribute(popup, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof(disabled));
        // Synthetic WM_HOTKEY does not grant the foreground permission of a real hotkey.
        // Join the current foreground queue briefly to activate this test-owned window.
        const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD testThread = GetCurrentThreadId();
        const bool attached = foregroundThread && foregroundThread != testThread &&
                              AttachThreadInput(testThread, foregroundThread, TRUE);
        ShowWindow(host, SW_SHOW);
        SetForegroundWindow(host);
        SetFocus(host);
        if (attached)
            AttachThreadInput(testThread, foregroundThread, FALSE);
        UpdateWindow(host);
        ShowWindow(popup, SW_SHOWNOACTIVATE);
        UpdateWindow(popup);
        DwmFlush();
        GetWindowRect(popup, &bounds);
        if (GetForegroundWindow() != host)
        {
            wchar_t foregroundClass[128]{};
            GetClassNameW(GetForegroundWindow(), foregroundClass, 128);
            DWORD foregroundProcess = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
            std::string diagnostic = "Cannot activate hover-menu test host. Foreground class: ";
            for (const wchar_t *p = foregroundClass; *p; ++p)
                diagnostic += static_cast<char>(*p);
            diagnostic += "; process " + std::to_string(foregroundProcess) +
                          "; test process " + std::to_string(GetCurrentProcessId());
            DestroyWindow(popup);
            DestroyWindow(host);
            throw std::runtime_error(diagnostic);
        }
    }
    ~SmokeHoverPopup()
    {
        DestroyWindow(popup);
        DestroyWindow(host);
    }
};
const std::array<Color, 6> PersistenceTestColors = {
    Palette[0], rgb(12, 34, 56), rgb(210, 87, 133), rgb(15, 120, 220), rgb(8, 91, 200), rgb(90, 35, 170)};
const std::array<uint8_t, 6> PersistenceTestStyles = {0, 0, 1, 3, 2, 1};
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    app.instance = instance;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
    {
        error(nullptr, "Cannot initialize Windows COM.");
        return 1;
    }
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool selfTest = false, trayOnly = false, snipNow = false, verifyPreferences = false;
    for (int i = 1; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"--self-test") == 0)
            selfTest = true;
        else if (wcscmp(argv[i], L"--tray") == 0)
            trayOnly = true;
        else if (wcscmp(argv[i], L"--snip") == 0)
            snipNow = true;
        else if (wcscmp(argv[i], L"--smoke-test") == 0)
            app.smoke = true;
        else if (wcscmp(argv[i], L"--verify-smoke-preferences") == 0)
            verifyPreferences = true;
    }
    LocalFree(argv);
    int result = 0;
    HANDLE mutex = nullptr;
    try
    {
        if (verifyPreferences)
        {
            wchar_t testDirectory[32768]{};
            GetCurrentDirectoryW(32768, testDirectory);
            app.iniPath = std::wstring(testDirectory) + L"\\smoke-settings.ini";
            loadToolPreferences();
            if (app.colors != PersistenceTestColors || app.styles != PersistenceTestStyles ||
                app.tool != Tool::Select || app.toolPreferencesDirty)
                throw std::runtime_error("Tool preferences did not survive a complete process exit.");
            writeTestReport(L"preference-test-results.txt",
                "PASS: a fresh process restored every tool's style and custom color after full exit; "
                "active tool unchanged, no pending preference write.\n");
        }
        else if (selfTest)
        {
            app.graphics.test();
            writeTestReport(L"self-test-results.txt",
                            "PASS: model history, cancellation, hit testing, resizing, coordinate "
                            "transforms, cropping, pen/circle/arrow/check composition, "
                            "solid/dashed/dotted line patterns, outlined/curved/straight gloss arrow artwork and "
                            "selection, PNG "
                            "pixel-perfect round trip, moved annotations.\n");
        }
        else
        {
            mutex = CreateMutexW(nullptr, FALSE, L"Local\\JackSnip.SingleInstance.1");
            if (!mutex)
                throw std::runtime_error("Cannot initialize the app instance.");
            if (GetLastError() == ERROR_ALREADY_EXISTS && !app.smoke)
            {
                HWND existing = FindWindowW(MainClass, nullptr);
                if (existing)
                {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(existing, &pid);
                    AllowSetForegroundWindow(pid);
                    PostMessageW(existing, LaunchMessage, snipNow, 0);
                }
                CloseHandle(mutex);
                CoUninitialize();
                return 0;
            }
            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
            InitCommonControlsEx(&controls);
            app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
            registerClasses();
            auto path = executablePath();
            app.iniPath = path.substr(0, path.find_last_of(L"\\/") + 1) + L"JackSnip.ini";
            if (app.smoke)
            {
                wchar_t testDirectory[32768]{};
                GetCurrentDirectoryW(32768, testDirectory);
                app.iniPath = std::wstring(testDirectory) + L"\\smoke-settings.ini";
            }
            else
                loadToolPreferences();
            app.hotkey = static_cast<WORD>(
                GetPrivateProfileIntW(L"Settings", L"Hotkey", app.hotkey, app.iniPath.c_str()));
            HDC dc = GetDC(nullptr);
            float dpi = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
            ReleaseDC(nullptr, dc);
            app.dialogFont =
                CreateFontW(-static_cast<int>(14 * dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            int width = std::min(static_cast<int>(1050 * dpi),
                                 static_cast<int>(work.right - work.left)),
                height =
                    std::min(static_cast<int>(740 * dpi), static_cast<int>(work.bottom - work.top));
            HWND window = CreateWindowExW(0, MainClass, L"Snipper", WS_OVERLAPPEDWINDOW,
                                          work.left + (work.right - work.left - width) / 2,
                                          work.top + (work.bottom - work.top - height) / 2, width,
                                          height, nullptr, createMenu(), instance, nullptr);
            if (!window)
                throw std::runtime_error("Cannot create the editor window.");
            // The smoke process must not compete with the user's running global shortcut.
            WORD initial = app.smoke ? 0 : app.hotkey;
            app.hotkey = 0;
            if (!registerShortcut(initial))
                status(L"Shortcut unavailable - change it in Settings");
            if (!trayOnly || !app.tray)
            {
                ShowWindow(window, show);
                UpdateWindow(window);
            }
            if (snipNow)
                startSnip();
            if (app.smoke)
            {
                // Exercise real Win32, Direct2D, and capture initialization without changing the
                // clipboard.
                ShowWindow(window, SW_SHOWNOACTIVATE);
                UpdateWindow(window);
                DwmFlush();
                saveBytes(L"smoke-test-home.png", app.graphics.png(renderEditorPreview()));
                auto clickButton = [&](int id, bool cancel = false) {
                    buildButtons();
                    const auto found =
                        std::find_if(app.buttons.begin(), app.buttons.end(),
                                     [&](const Button &b) { return b.command == id; });
                    if (found == app.buttons.end() || !enabled(id))
                        throw std::runtime_error(
                            "Toolbar action is missing or unexpectedly disabled.");
                    const Rect r = found->rect;
                    auto point = MAKELPARAM(static_cast<int>((r.left + r.right) / 2 * app.dpi),
                                            static_cast<int>((r.top + r.bottom) / 2 * app.dpi));
                    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
                    if (!app.pressed || GetCapture() != window)
                        throw std::runtime_error(
                            "Toolbar press feedback did not capture the pointer.");
                    SendMessageW(window, WM_LBUTTONUP, 0, cancel ? MAKELPARAM(2, 2) : point);
                    if (app.pressed || GetCapture() == window)
                        throw std::runtime_error("Toolbar release left the pointer captured.");
                };
                // Verify actual capture pixels against a known backing surface to catch
                // editor content and partially faded compositor surfaces alike.
                RECT editorBounds{};
                GetWindowRect(window, &editorBounds);
                HWND backing = CreateWindowExW(
                    WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"STATIC", L"Capture regression backing",
                    WS_POPUP | SS_BLACKRECT, editorBounds.left, editorBounds.top,
                    editorBounds.right - editorBounds.left, editorBounds.bottom - editorBounds.top,
                    nullptr, nullptr, instance, nullptr);
                if (!backing)
                    throw std::runtime_error("Cannot create capture regression backing.");
                const BOOL noBackingAnimation = TRUE;
                DwmSetWindowAttribute(backing, DWMWA_TRANSITIONS_FORCEDISABLED, &noBackingAnimation,
                                      sizeof(noBackingAnimation));
                ShowWindow(backing, SW_SHOWNOACTIVATE);
                UpdateWindow(backing);
                DwmFlush();
                const Bitmap backingPixels = captureDesktop(editorBounds.left, editorBounds.top,
                                                            editorBounds.right - editorBounds.left,
                                                            editorBounds.bottom - editorBounds.top);
                SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                app.image = Bitmap::create(640, 360);
                std::fill(app.image.pixels.begin(), app.image.pixels.end(), 255);
                Annotation unsaved;
                unsaved.points = {{20, 20}, {180, 80}};
                app.document.items.push_back(unsaved);
                app.dirty = true;
                {
                    SmokePromptAction cancel(IDCANCEL);
                    command(NewSnip);
                    DWORD cloaked = 0;
                    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
                    if (!cancel.captureBlocked || !cancel.repeatIgnored)
                        throw std::runtime_error(
                            "Repeated snips are not blocked during confirmation.");
                    if (!cancel.shown || !cancel.editorVisible || !cancel.promptOwned ||
                        !IsWindowVisible(window) || cloaked || app.capturePending || app.overlay ||
                        !app.dirty || app.document.items.size() != 1)
                        throw std::runtime_error("Canceling the new-snip prompt lost the editor.");
                }
                UpdateWindow(window);
                SmokePromptAction discard(IDNO);
                command(NewSnip);
                if (!discard.captureBlocked || !discard.repeatIgnored)
                    throw std::runtime_error("Repeated snips are not blocked during confirmation.");
                if (!discard.shown || !discard.editorVisible || !discard.promptOwned)
                    throw std::runtime_error("Capture confirmation did not stay over the editor.");
                if (IsWindowVisible(window) || !app.capturePending)
                    throw std::runtime_error("Snip did not immediately hide the editor.");
                SendMessageW(window, WM_TIMER, CaptureTimer, 0);
                if (!app.overlay)
                    throw std::runtime_error("Snip did not create its selection overlay.");
                RECT dialogBounds = discard.bounds;
                InflateRect(&dialogBounds, 16, 16);
                if (dialogBounds.left < editorBounds.left || dialogBounds.top < editorBounds.top ||
                    dialogBounds.right > editorBounds.right ||
                    dialogBounds.bottom > editorBounds.bottom)
                    throw std::runtime_error("Capture confirmation fell outside its test backing.");
                auto dialogCapture = app.desktop.crop(
                    dialogBounds.left - app.virtualX, dialogBounds.top - app.virtualY,
                    dialogBounds.right - dialogBounds.left, dialogBounds.bottom - dialogBounds.top);
                auto dialogReference = backingPixels.crop(
                    dialogBounds.left - editorBounds.left, dialogBounds.top - editorBounds.top,
                    dialogBounds.right - dialogBounds.left, dialogBounds.bottom - dialogBounds.top);
                if (dialogCapture.pixels != dialogReference.pixels)
                    throw std::runtime_error("Save confirmation or its fade leaked into capture.");
                // Compare against the surface as actually presented. The STATIC
                // rectangle's shade depends on the current Windows theme.
                for (float fraction : {.25f, .5f, .75f})
                {
                    int x = editorBounds.left - app.virtualX +
                            static_cast<int>((editorBounds.right - editorBounds.left) * fraction);
                    int y = editorBounds.top - app.virtualY +
                            static_cast<int>((editorBounds.bottom - editorBounds.top) * fraction);
                    size_t i = (static_cast<size_t>(y) * app.desktop.width + x) * 4;
                    int localX = x + app.virtualX - editorBounds.left,
                        localY = y + app.virtualY - editorBounds.top;
                    size_t reference =
                        (static_cast<size_t>(localY) * backingPixels.width + localX) * 4;
                    if (app.desktop.pixels[i] != backingPixels.pixels[reference] ||
                        app.desktop.pixels[i + 1] != backingPixels.pixels[reference + 1] ||
                        app.desktop.pixels[i + 2] != backingPixels.pixels[reference + 2])
                        throw std::runtime_error("Editor or fade animation leaked into capture.");
                }
                // The shortcut path must also exclude the visible editor, with no fade or delay.
                cancelCapture();
                app.dirty = false;
                SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                UpdateWindow(window);
                DwmFlush();
                SendMessageW(window, WM_HOTKEY, app.hotkeyId, 0);
                if (!app.overlay || app.capturePending)
                    throw std::runtime_error("Hotkey did not open selection immediately.");
                const auto instantBacking = app.desktop.crop(editorBounds.left - app.virtualX,
                    editorBounds.top - app.virtualY, backingPixels.width, backingPixels.height);
                if (instantBacking.pixels != backingPixels.pixels)
                    throw std::runtime_error("Visible editor or its fade leaked into instant hotkey capture.");
                DestroyWindow(backing);
                SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                HWND overlay = app.overlay;
                SendMessageW(overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
                SendMessageW(overlay, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(220, 160));
                UpdateWindow(overlay);
                COLORREF overlayPixel = GetPixel(app.overlayDC, 100, 90);
                size_t sourcePixel = (static_cast<size_t>(90) * app.desktop.width + 100) * 4;
                Color sourceColor =
                    rgb(app.desktop.pixels[sourcePixel + 2], app.desktop.pixels[sourcePixel + 1],
                        app.desktop.pixels[sourcePixel]);
                if (overlayPixel != sourceColor)
                    throw std::runtime_error("Selection overlay source pixel test failed.");
                SendMessageW(overlay, WM_LBUTTONUP, 0, MAKELPARAM(220, 160));
                if (app.image.width != 180 || app.image.height != 120 || app.overlay)
                    throw std::runtime_error("Rectangle capture interaction failed.");
                DWORD cloaked = 0;
                check(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)),
                      "Cannot verify editor restoration.");
                if (app.tool != Tool::Pen || !IsWindowVisible(window) || cloaked)
                    throw std::runtime_error(
                        "Captured image did not restore the editor with Pen selected.");
                // The popup disappears when another window takes focus, just like hover menus.
                // Its pixels must already be frozen before an overlay or confirmation activates.
                for (int mode = 0; mode < 4; ++mode)
                {
                    app.document.clear();
                    app.dirty = mode == 1 || mode == 2;
                    if (app.dirty)
                        app.document.items.push_back(unsaved);
                    if (mode == 3)
                        ShowWindow(window, SW_HIDE);
                    SmokeHoverPopup popup(editorBounds.left + 40, editorBounds.top + 100);
                    const auto reference = captureDesktop(popup.bounds.left, popup.bounds.top,
                        popup.bounds.right - popup.bounds.left, popup.bounds.bottom - popup.bounds.top);
                    if (app.dirty)
                    {
                        SmokePromptAction prompt(mode == 2 ? IDCANCEL : IDNO);
                        SendMessageW(window, WM_HOTKEY, app.hotkeyId, 0);
                        if (!prompt.shown || !prompt.editorVisible || !prompt.promptOwned ||
                            !prompt.repeatIgnored)
                            throw std::runtime_error("Instant capture confirmation or repeated-hotkey guard failed.");
                    }
                    else
                        SendMessageW(window, WM_HOTKEY, app.hotkeyId, 0);
                    if (!popup.dismissed)
                        throw std::runtime_error("Hover-menu fixture did not dismiss on focus loss.");
                    if (mode == 2)
                    {
                        if (app.overlay || app.capturePending || !app.desktop.empty() ||
                            !app.dirty || app.document.items.size() != 1 ||
                            app.image.width != 180 || app.image.height != 120 || !IsWindowVisible(window))
                            throw std::runtime_error("Canceling instant capture lost the previous edited snip.");
                    }
                    else
                    {
                        if (!app.overlay || app.capturePending)
                            throw std::runtime_error("Hotkey capture still waits for a capture timer.");
                        const auto frozen = app.desktop.crop(popup.bounds.left - app.virtualX,
                            popup.bounds.top - app.virtualY, reference.width, reference.height);
                        if (frozen.pixels != reference.pixels)
                            throw std::runtime_error("Instant capture lost the hover popup before freezing the screen.");
                        SendMessageW(app.overlay, WM_KEYDOWN, VK_ESCAPE, 0);
                        if (app.overlay || !app.desktop.empty() || !IsWindowVisible(window) ||
                            app.image.width != 180 || app.image.height != 120)
                            throw std::runtime_error("Canceling instant selection failed to restore the previous snip.");
                    }
                    SendMessageW(window, WM_TIMER, CaptureTimer, 0);
                    if (app.overlay || app.capturePending || !app.desktop.empty())
                        throw std::runtime_error("A stale capture timer reopened canceled instant selection.");
                }
                app.document.clear();
                app.dirty = false;
                app.displayBitmap.reset();
                app.image = Bitmap::create(640, 360);
                for (int y = 0; y < app.image.height; ++y)
                    for (int x = 0; x < app.image.width; ++x)
                    {
                        size_t i = (static_cast<size_t>(y) * app.image.width + x) * 4;
                        bool stripe = (x / 40 + y / 40) % 2;
                        app.image.pixels[i] = stripe ? 245 : 230;
                        app.image.pixels[i + 1] = stripe ? 238 : 226;
                        app.image.pixels[i + 2] = stripe ? 228 : 211;
                        app.image.pixels[i + 3] = 255;
                    }
                updateView();
                Point a = app.view.toScreen({20, 20}), b = app.view.toScreen({180, 80});
                SendMessageW(
                    window, WM_LBUTTONDOWN, MK_LBUTTON,
                    MAKELPARAM(static_cast<int>(a.x * app.dpi), static_cast<int>(a.y * app.dpi)));
                SendMessageW(
                    window, WM_MOUSEMOVE, MK_LBUTTON,
                    MAKELPARAM(static_cast<int>(b.x * app.dpi), static_cast<int>(b.y * app.dpi)));
                SendMessageW(
                    window, WM_LBUTTONUP, 0,
                    MAKELPARAM(static_cast<int>(b.x * app.dpi), static_cast<int>(b.y * app.dpi)));
                if (app.document.items.empty())
                    throw std::runtime_error("UI pen interaction failed.");
                clickButton(Undo, true);
                if (app.document.items.size() != 1)
                    throw std::runtime_error("Releasing outside the toolbar still activated Undo.");
                clickButton(Undo);
                if (!app.document.items.empty())
                    throw std::runtime_error("UI undo failed.");
                clickButton(Redo);
                if (app.document.items.size() != 1)
                    throw std::runtime_error("UI redo failed.");
                auto dragImage = [&](Point from, Point to) {
                    Point start = app.view.toScreen(from), end = app.view.toScreen(to);
                    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(static_cast<int>(start.x * app.dpi),
                                            static_cast<int>(start.y * app.dpi)));
                    SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON,
                                 MAKELPARAM(static_cast<int>(end.x * app.dpi),
                                            static_cast<int>(end.y * app.dpi)));
                    SendMessageW(window, WM_LBUTTONUP, 0,
                                 MAKELPARAM(static_cast<int>(end.x * app.dpi),
                                            static_cast<int>(end.y * app.dpi)));
                };
                command(CircleTool);
                dragImage({250, 40}, {360, 130});
                if (app.document.items.size() != 2 || app.tool != Tool::Select)
                    throw std::runtime_error("Circle placement failed.");
                dragImage({250, 40}, {235, 25});
                command(ColorFirst + 4);
                if (app.document.items[1].bounds().width() < 120 ||
                    app.document.items[1].color != Palette[4] ||
                    app.colors[static_cast<size_t>(Tool::Circle)] != Palette[4])
                    throw std::runtime_error("Circle resize or recolor failed.");
                command(ArrowTool);
                dragImage({80, 150}, {230, 270});
                dragImage({230, 270}, {300, 240});
                command(ColorFirst + 5);
                if (app.document.items.size() != 3 ||
                    length(app.document.items[2].b - Point{300, 240}) > 2 ||
                    app.colors[static_cast<size_t>(Tool::Arrow)] != Palette[5])
                    throw std::runtime_error("Arrow endpoint rotation failed.");
                command(CheckTool);
                dragImage({470, 100}, {470, 100});
                dragImage({470, 100}, {490, 140});
                auto checkBounds = app.document.items.back().bounds();
                dragImage({checkBounds.right, checkBounds.bottom},
                          {checkBounds.right + 20, checkBounds.bottom + 20});
                if (app.document.items.size() != 4 ||
                    app.document.items.back().color != Palette[3] ||
                    app.document.items.back().bounds().width() < 70)
                    throw std::runtime_error("Check placement, move, or resize failed.");
                command(DeleteSelected);
                if (app.document.items.size() != 3)
                    throw std::runtime_error("Delete selection failed.");
                command(Undo);
                if (app.document.items.size() != 4)
                    throw std::runtime_error("Undo deletion failed.");
                for (int style = 0; style < 3; ++style)
                {
                    command(styleCommand(Tool::Line, style));
                    float y = 300.0f + style * 16;
                    dragImage({20, y}, {220, y});
                    dragImage({220, y}, {240, y});
                    const auto &line = app.document.items.back();
                    if (line.kind != Tool::Line || line.style != style ||
                        length(line.b - Point{240, y}) > 2 || !line.hit({130, y}, 1))
                        throw std::runtime_error(
                            "Line style, selection, or endpoint editing failed.");
                }
                for (int style = 1; style <= 3; ++style)
                {
                    command(styleCommand(Tool::Arrow, style));
                    dragImage({400, 190.0f + style * 20}, {550, 220.0f + style * 20});
                    const auto &arrow = app.document.items.back();
                    if (arrow.kind != Tool::Arrow || arrow.style != style ||
                        !arrow.hit(arrow.arrowSpine(.5f), 0))
                        throw std::runtime_error(
                            "Outlined, curved, or straight gloss arrow placement/selection failed.");
                }
                auto flattened = app.graphics.flatten(app.image, app.document.items);
                saveBytes(L"smoke-test-export.png", app.graphics.png(flattened));
                UpdateWindow(window);
                DwmFlush();
                saveBytes(L"smoke-test-editor.png", app.graphics.png(renderEditorPreview()));
                // Render actual compact and high-DPI layouts using the same editor paint path.
                RECT normalBounds{};
                GetWindowRect(window, &normalBounds);
                const float normalDpi = app.dpi;
                for (float scale : {1.0f, 1.5f, 2.0f})
                {
                    app.dpi = scale;
                    SetWindowPos(window, nullptr, 0, 0, static_cast<int>(850 * scale),
                                 static_cast<int>(430 * scale),
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                    // The compositor may deliver a DPI change while resizing; this fixture
                    // explicitly sets the scale of the offscreen renderer afterward.
                    app.dpi = scale;
                    buildButtons();
                    const auto bounds = clientDips();
                    for (const auto &b : app.buttons)
                        if (b.rect.left < 0 || b.rect.right > bounds.right || b.rect.top < 0 ||
                            b.rect.bottom > ToolbarHeight)
                            throw std::runtime_error("Compact toolbar control is clipped.");
                    saveBytes(L"smoke-test-layout-" +
                                  std::to_wstring(static_cast<int>(scale * 100)) + L".png",
                              app.graphics.png(renderEditorPreview()));
                }
                app.dpi = normalDpi;
                if (app.target)
                    app.target->SetDpi(normalDpi * 96, normalDpi * 96);
                SetWindowPos(window, nullptr, normalBounds.left, normalBounds.top,
                             normalBounds.right - normalBounds.left,
                             normalBounds.bottom - normalBounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
                openSettings();
                if (!app.hotkeyControl ||
                    static_cast<WORD>(SendMessageW(app.hotkeyControl, HKM_GETHOTKEY, 0, 0)) !=
                        app.hotkey)
                    throw std::runtime_error("Shortcut settings initialization failed.");
                auto originalIni = app.iniPath;
                wchar_t testDirectory[32768]{};
                GetCurrentDirectoryW(32768, testDirectory);
                app.iniPath = std::wstring(testDirectory) + L"\\smoke-settings.ini";
                SendMessageW(app.settingsWindow, WM_COMMAND, IDOK, 0);
                if (app.settingsWindow ||
                    static_cast<WORD>(GetPrivateProfileIntW(L"Settings", L"Hotkey", 0,
                                                            app.iniPath.c_str())) != app.hotkey)
                    throw std::runtime_error("Shortcut settings save failed.");
                app.iniPath = originalIni;
                for (int i = 1; i < 6; ++i)
                {
                    const Tool tool = static_cast<Tool>(i);
                    if (i >= static_cast<int>(Tool::Circle))
                        command(styleCommand(tool, tool == Tool::Check ? 1 : PersistenceTestStyles[i]));
                    else
                        selectTool(tool);
                    changeColor(PersistenceTestColors[i]);
                }
                const auto closeColors = app.colors;
                const auto closeStyles = app.styles;
                app.dirty = false;
                SendMessageW(window, WM_CLOSE, 0, 0);
                if (hasImage() || IsWindowVisible(window) || app.toolPreferencesDirty)
                    throw std::runtime_error("Closing to the tray did not save tool preferences.");
                app.colors.fill(Palette[0]);
                app.styles.fill(0);
                const auto closedTool = app.tool;
                loadToolPreferences();
                if (app.colors != closeColors || app.styles != closeStyles || app.tool != closedTool)
                    throw std::runtime_error("Closing lost tool colors/styles or changed the active tool.");
                // A later change must be written by the full-exit path, not the earlier close.
                app.image = Bitmap::create(10, 10);
                command(styleCommand(Tool::Check, 2));
                if (activeColor() != PersistenceTestColors[static_cast<size_t>(Tool::Check)])
                    throw std::runtime_error("Reopening a tool lost its previous custom color.");
                writeTestReport(
                    L"smoke-test-results.txt",
                    "PASS: real unsaved-snip Cancel/No dialogs owned over the visible editor, "
                    "editor restoration after Cancel, "
                    "repeated hotkeys blocked while confirmation is open, "
                    "immediate capture with no confirmation dialog/fade pixels, "
                    "no editor/fade pixels in capture, restored editor with Pen selected, "
                    "instant hotkey freezes focus-dismissed hover popup pixels before overlay/save prompt, "
                    "visible and hidden editor, instant cancellation preserves the previous snip, "
                    "native window, Direct2D editor, live desktop capture, selection overlay "
                    "original pixels, mouse rectangle selection and cropping, mouse drawing, all "
                    "stickers, solid/dashed/dotted lines and endpoint editing, outlined/curved "
                    "arrows, straight gloss arrow, "
                    "move/resize/recolor, arrow endpoint rotation, delete, toolbar press/release "
                    "and cancellation, mouse undo/redo, compact layouts at 100/150/200% DPI, "
                    "annotated PNG export, settings dialog and persistence, per-tool styles/custom colors "
                    "saved on close, selected-shape recoloring remembers the correct tool.\n");
                SetTimer(window, SmokeTimer, 1000, nullptr);
            }
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                if (app.settingsWindow && IsDialogMessageW(app.settingsWindow, &message))
                    continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
    catch (const std::exception &exception)
    {
        result = 1;
        if (selfTest || app.smoke || verifyPreferences)
            writeTestReport(verifyPreferences ? L"preference-test-results.txt" :
                            selfTest ? L"self-test-results.txt" : L"smoke-test-results.txt",
                            std::string("FAIL: ") + exception.what() + "\n");
        else
            error(app.window, exception.what());
        if (app.window && IsWindow(app.window))
        {
            app.dirty = false;
            DestroyWindow(app.window);
        }
    }
    app.displayBitmap.reset();
    app.target.reset();
    app.graphics.roundStroke.reset();
    app.graphics.dashStroke.reset();
    app.graphics.dotStroke.reset();
    app.graphics.titleFont.reset();
    app.graphics.smallFont.reset();
    app.graphics.font.reset();
    app.graphics.textFactory.reset();
    app.graphics.factory.reset();
    if (app.dialogFont)
        DeleteObject(app.dialogFont);
    if (mutex)
        CloseHandle(mutex);
    CoUninitialize();
    return result;
}
