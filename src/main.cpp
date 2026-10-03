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
constexpr float ToolbarHeight = 100, StatusHeight = 28;
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
    HWND window = nullptr, overlay = nullptr, settingsWindow = nullptr, hotkeyControl = nullptr;
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
    std::array<Color, 6> colors = {Palette[0], Palette[0], Palette[0], Palette[0], Palette[3], Palette[0]};
    std::array<uint8_t, 6> styles{};
    float thickness = 4, dpi = 1;
    View view;
    bool fit = true, dirty = false, capturePending = false, exiting = false, tray = false,
         spaceDown = false;
    bool selecting = false, changed = false, smoke = false;
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
    int hover = 0;
    UINT taskbarCreated = 0;
} app;

LRESULT CALLBACK mainProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK overlayProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK settingsProcedure(HWND, UINT, WPARAM, LPARAM);
void command(int id);
void startSnip();
void hideEditorForCapture();
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
    const int result = MessageBoxW(app.window, question, L"Snipper", MB_YESNOCANCEL | MB_ICONQUESTION);
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
void buildButtons()
{
    app.buttons.clear();
    float x = 14;
    auto add = [&](int id, const wchar_t *text, float width, float y = 12) {
        app.buttons.push_back({{x, y, x + width, y + 32}, id, text});
        x += width + 6;
    };
    auto addStyleTool = [&](int id, const wchar_t *text, float width, int menu) {
        add(id, text, width - 22);
        add(menu, L"\u25BE", 18);
    };
    add(NewSnip, L"+  Snip", 82);
    add(Copy, L"Copy", 64);
    add(Save, L"Save", 64);
    x += 10;
    add(SelectTool, L"Select", 66);
    add(PenTool, L"Pen", 55);
    addStyleTool(CircleTool, L"Circle", 96, CircleStyleMenu);
    addStyleTool(ArrowTool, L"Arrow", 96, ArrowStyleMenu);
    addStyleTool(CheckTool, L"Check", 96, CheckStyleMenu);
    addStyleTool(LineTool, L"Line", 88, LineStyleMenu);
    x = 54;
    for (int i = 0; i < 8; ++i)
    {
        app.buttons.push_back({{x, 61, x + 23, 84}, ColorFirst + i, L""});
        x += 29;
    }
    add(CustomColor, L"Custom", 62, 57);
    x += 15;
    add(SizeDown, L"-", 27, 57);
    x += 44;
    add(SizeUp, L"+", 27, 57);
    x += 16;
    add(Fit, L"Fit", 43, 57);
    add(Actual, L"100%", 54, 57);
}
bool enabled(int id)
{
    if (id == NewSnip)
        return !app.capturePending && !app.overlay;
    if (id == Copy || id == Save || id == SaveAs || id == Fit || id == Actual ||
        (id >= SelectTool && id <= LineTool) ||
        (id >= CircleStyleMenu && id <= LineStyleMenu))
        return hasImage();
    return true;
}
bool active(int id)
{
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
    rt->BeginDraw();
    rt->Clear(color(rgb(239, 242, 247)));
    fill({0, 0, client.right, ToolbarHeight}, rgb(255, 255, 255));
    fill({0, ToolbarHeight - 1, client.right, ToolbarHeight}, rgb(220, 225, 233));
    for (const auto &button : app.buttons)
    {
        auto r = button.rect;
        bool on = active(button.command) ||
                  (button.command == CircleStyleMenu && app.tool == Tool::Circle) ||
                  (button.command == ArrowStyleMenu && app.tool == Tool::Arrow) ||
                  (button.command == CheckStyleMenu && app.tool == Tool::Check) ||
                  (button.command == LineStyleMenu && app.tool == Tool::Line),
             over = app.hover == button.command,
             available = enabled(button.command);
        if (button.command >= ColorFirst && button.command < ColorFirst + 8)
        {
            Color value = Palette[button.command - ColorFirst];
            brush->SetColor(color(value));
            rt->FillRoundedRectangle(
                D2D1::RoundedRect({r.left + 2, r.top + 2, r.right - 2, r.bottom - 2}, 5, 5),
                brush.get());
            brush->SetColor(color(activeColor() == value ? rgb(37, 99, 235) : rgb(210, 215, 225)));
            rt->DrawRoundedRectangle(D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, 6, 6),
                                     brush.get(), activeColor() == value ? 2.0f : 1.0f);
            continue;
        }
        Color bg =
            button.command == NewSnip
                ? rgb(37, 99, 235)
                : (on ? rgb(224, 235, 255) : (over ? rgb(241, 245, 249) : rgb(255, 255, 255)));
        brush->SetColor(color(bg));
        rt->FillRoundedRectangle(D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, 6, 6),
                                 brush.get());
        if (button.command != NewSnip)
        {
            brush->SetColor(color(on ? rgb(147, 181, 244) : rgb(220, 225, 233)));
            rt->DrawRoundedRectangle(D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, 6, 6),
                                     brush.get(), 1);
        }
        Color fg = !available
                       ? rgb(160, 168, 180)
                       : (button.command == NewSnip ? rgb(255, 255, 255)
                                                    : (on ? rgb(29, 78, 216) : rgb(51, 65, 85)));
        if (button.command >= CircleTool && button.command <= LineTool)
        {
            Annotation icon;
            icon.kind = static_cast<Tool>(button.command - SelectTool);
            icon.color = button.command == CheckTool && available ? Palette[3] : fg;
            icon.thickness = 1.7f;
            icon.style = app.styles[static_cast<size_t>(icon.kind)];
            icon.a = {r.left + 10, r.top + 8};
            icon.b = {r.left + 26, r.top + 24};
            if (icon.kind == Tool::Arrow || icon.kind == Tool::Line)
            {
                icon.a.y = r.top + 23;
                icon.b.y = r.top + 9;
            }
            app.graphics.drawAnnotations(rt, {icon});
            text(button.label, {r.left + 33, r.top, r.right - 5, r.bottom}, fg,
                 app.graphics.font.get());
        }
        else
            text(button.label, r, fg, app.graphics.font.get(), true);
    }
    text(L"Color", {14, 57, 51, 89}, rgb(100, 116, 139), app.graphics.smallFont.get());
    auto minus = std::find_if(app.buttons.begin(), app.buttons.end(),
                              [](const Button &b) { return b.command == SizeDown; });
    float size = selected() && app.document.items[app.document.selected].kind != Tool::Check
                     ? app.document.items[app.document.selected].thickness
                     : app.thickness;
    if (minus != app.buttons.end())
        text(std::to_wstring(static_cast<int>(size)) + L" px",
             {minus->rect.right + 5, 57, minus->rect.right + 44, 89}, rgb(71, 85, 105),
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
        fill({o.x + 3, o.y + 3, o.x + w + 3, o.y + h + 3}, rgb(210, 216, 226));
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
            brush->SetColor(color(rgb(37, 99, 235), .7f));
            if (item.kind != Tool::Arrow && item.kind != Tool::Line)
                rt->DrawRectangle({a.x, a.y, b.x, b.y}, brush.get(), 1);
            for (auto p : handles(item))
            {
                p = app.view.toScreen(p);
                brush->SetColor(color(rgb(255, 255, 255)));
                rt->FillRectangle({p.x - 4, p.y - 4, p.x + 4, p.y + 4}, brush.get());
                brush->SetColor(color(rgb(37, 99, 235)));
                rt->DrawRectangle({p.x - 4, p.y - 4, p.x + 4, p.y + 4}, brush.get(), 1.5f);
            }
        }
        rt->PopAxisAlignedClip();
    }
    else
    {
        float middle = canvas.top + canvas.height() / 2;
        text(L"Grab it. Mark it. Paste it.", {0, middle - 65, client.right, middle - 18},
             rgb(30, 41, 59), app.graphics.titleFont.get(), true);
        text(LOBYTE(app.hotkey)
                 ? L"Click Snip or press " + hotkeyName(app.hotkey) + L" to select an area."
                 : L"Click Snip to select an area.",
             {0, middle - 10, client.right, middle + 20}, rgb(100, 116, 139),
             app.graphics.font.get(), true);
        text(L"Ctrl+C copies your image. Ctrl+S saves a PNG.",
             {0, middle + 20, client.right, middle + 50}, rgb(100, 116, 139),
             app.graphics.font.get(), true);
    }
    fill({0, client.bottom - StatusHeight, client.right, client.bottom}, rgb(255, 255, 255));
    std::wstring message = app.status;
    if (message.empty())
    {
        if (hasImage())
        {
            const wchar_t *hints[] = {
                L"Select: drag to move; corner handles resize; Delete removes",
                L"Pen: drag to draw; Ctrl+Z undoes", L"Circle: drag to draw; Shift makes a circle",
                L"Arrow: drag to draw; select and drag endpoints to turn",
                L"Check: click to place; drag to size",
                L"Line: drag to draw; Shift snaps angle; drag endpoints to resize"};
            message = hints[static_cast<int>(app.tool)];
        }
        else
            message =
                L"Ready  |  Shortcut: " + hotkeyName(app.hotkey) + L"  |  Settings in the menu";
    }
    text(message,
         {14, client.bottom - StatusHeight, client.right - (hasImage() ? 185 : 14), client.bottom},
         rgb(100, 116, 139), app.graphics.smallFont.get());
    if (hasImage())
        text(std::to_wstring(static_cast<int>(std::round(app.view.scale * app.dpi * 100))) +
                 L"%  |  " + std::to_wstring(app.image.width) + L" x " +
                 std::to_wstring(app.image.height),
             {client.right - 165, client.bottom - StatusHeight, client.right - 12, client.bottom},
             rgb(100, 116, 139), app.graphics.smallFont.get());
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
    app.colors[static_cast<size_t>(app.tool)] = value;
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
    if (!middle && screen.y < ToolbarHeight)
    {
        for (const auto &button : app.buttons)
            if (button.rect.contains(screen))
            {
                if (enabled(button.command))
                    command(button.command);
                return;
            }
        return;
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
                                   ? Drag::Endpoint : Drag::Resize;
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
            item.b = limited(app.dragStart + Point{std::cos(angle), std::sin(angle)} * length(delta));
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
void mouseUp()
{
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
                    L"Snipper saves PNG images. Use a filename ending in .png.",
                    L"Save as PNG", MB_OK | MB_ICONINFORMATION);
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
        const wchar_t *const labels[][3] = {
            {L"Outline", L"Soft highlight", L"Dashed outline"},
            {L"Classic", L"Outlined", L"Curved gloss"},
            {L"Boxed", L"Circle badge", L"Simple check"},
            {L"Solid", L"Dashed", L"Dotted"}};
        HMENU menu = CreatePopupMenu();
        if (!menu)
            return;
        const size_t toolIndex = static_cast<size_t>(tool);
        const int base = StyleChoiceFirst + (static_cast<int>(toolIndex) -
                                             static_cast<int>(Tool::Circle)) * 3;
        for (int style = 0; style < 3; ++style)
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
    if (id >= StyleChoiceFirst && id < StyleChoiceFirst + 12)
    {
        const int option = id - StyleChoiceFirst;
        const Tool tool = static_cast<Tool>(static_cast<int>(Tool::Circle) + option / 3);
        app.styles[static_cast<size_t>(tool)] = static_cast<uint8_t>(option % 3);
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
void hideEditorForCapture()
{
    // SW_HIDE alone can leave a fading DWM surface in a screen capture. Suppress
    // transitions and cloak our own window before hiding it; DwmFlush in openOverlay
    // then waits for the compositor to present a desktop without the editor.
    const BOOL disabled = TRUE;
    DwmSetWindowAttribute(app.window, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof(disabled));
    DwmSetWindowAttribute(app.window, DWMWA_CLOAK, &disabled, sizeof(disabled));
    ShowWindow(app.window, SW_HIDE);
}
void startSnip()
{
    if (app.overlay || app.capturePending || app.settingsWindow)
        return;
    // Modal dialogs pump hotkeys too. Reserve the request before opening one,
    // so another rapid snip cannot start capture with an earlier dialog still open.
    app.capturePending = true;
    try
    {
        if (!canDiscard(true))
        {
            app.capturePending = false;
            repaint();
            return;
        }
        hideEditorForCapture();
        if (!SetTimer(app.window, CaptureTimer, 65, nullptr))
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
    app.virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    app.virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    DwmFlush();
    app.desktop = captureDesktop(app.virtualX, app.virtualY, width, height);
    app.dimDesktop = app.desktop;
    for (size_t i = 0; i < app.dimDesktop.pixels.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            app.dimDesktop.pixels[i + c] =
                static_cast<uint8_t>(app.dimDesktop.pixels[i + c] * .48f);
    app.selecting = false;
    app.overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, OverlayClass,
                                  L"Snipper selection", WS_POPUP, app.virtualX, app.virtualY,
                                  width, height, nullptr, nullptr, app.instance, nullptr);
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
        if (app.drag != Drag::None)
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
        info->ptMinTrackSize = {static_cast<LONG>(850 * d), static_cast<LONG>(360 * d)};
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
        mouseUp();
        return 0;
    case WM_CAPTURECHANGED:
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
            bool onCanvas = p.y / app.dpi >= ToolbarHeight && hasImage();
            SetCursor(LoadCursorW(
                nullptr, app.drag == Drag::Pan || app.spaceDown
                             ? IDC_SIZEALL
                             : (onCanvas && app.tool != Tool::Select ? IDC_CROSS : IDC_ARROW)));
            return TRUE;
        }
        break;
    case WM_HOTKEY:
        if (!app.settingsWindow)
            startSnip();
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
            app.exiting = true;
            DestroyWindow(hwnd);
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
    case WM_DESTROY:
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
            EnumThreadWindows(GetCurrentThreadId(), findPrompt,
                              reinterpret_cast<LPARAM>(current));
    }

  public:
    bool shown = false, editorVisible = false, promptOwned = false, captureBlocked = false, repeatIgnored = false,
         extraPrompt = false;
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
    bool selfTest = false, trayOnly = false, snipNow = false;
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
    }
    LocalFree(argv);
    int result = 0;
    HANDLE mutex = nullptr;
    try
    {
        if (selfTest)
        {
            app.graphics.test();
            writeTestReport(L"self-test-results.txt",
                            "PASS: model history, cancellation, hit testing, resizing, coordinate "
                            "transforms, cropping, pen/circle/arrow/check composition, "
                            "solid/dashed/dotted line patterns, outlined/curved arrow artwork and selection, PNG "
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
                        throw std::runtime_error("Repeated snips are not blocked during confirmation.");
                    if (!cancel.shown || !cancel.editorVisible || !cancel.promptOwned || !IsWindowVisible(window) ||
                        cloaked || app.capturePending || app.overlay || !app.dirty ||
                        app.document.items.size() != 1)
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
                command(Undo);
                if (!app.document.items.empty())
                    throw std::runtime_error("UI undo failed.");
                command(Redo);
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
                    app.document.items[1].color != Palette[4])
                    throw std::runtime_error("Circle resize or recolor failed.");
                command(ArrowTool);
                dragImage({80, 150}, {230, 270});
                dragImage({230, 270}, {300, 240});
                command(ColorFirst + 5);
                if (app.document.items.size() != 3 ||
                    length(app.document.items[2].b - Point{300, 240}) > 2)
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
                    command(StyleChoiceFirst + 9 + style);
                    float y = 300.0f + style * 16;
                    dragImage({20, y}, {220, y});
                    dragImage({220, y}, {240, y});
                    const auto &line = app.document.items.back();
                    if (line.kind != Tool::Line || line.style != style ||
                        length(line.b - Point{240, y}) > 2 || !line.hit({130, y}, 1))
                        throw std::runtime_error("Line style, selection, or endpoint editing failed.");
                }
                for (int style = 1; style <= 2; ++style)
                {
                    command(StyleChoiceFirst + 3 + style);
                    dragImage({400, 220}, {550, 290});
                    const auto &arrow = app.document.items.back();
                    if (arrow.kind != Tool::Arrow || arrow.style != style ||
                        !arrow.hit(arrow.arrowSpine(.5f), 0))
                        throw std::runtime_error("Outlined or curved arrow placement/selection failed.");
                }
                auto flattened = app.graphics.flatten(app.image, app.document.items);
                saveBytes(L"smoke-test-export.png", app.graphics.png(flattened));
                UpdateWindow(window);
                DwmFlush();
                saveBytes(L"smoke-test-editor.png", app.graphics.png(renderEditorPreview()));
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
                writeTestReport(
                    L"smoke-test-results.txt",
                    "PASS: real unsaved-snip Cancel/No dialogs owned over the visible editor, "
                    "editor restoration after Cancel, "
                    "repeated hotkeys blocked while confirmation is open, "
                    "immediate capture with no confirmation dialog/fade pixels, "
                    "no editor/fade pixels in capture, restored editor with Pen selected, "
                    "native window, Direct2D editor, live desktop capture, selection overlay "
                    "original pixels, mouse rectangle selection and cropping, mouse drawing, all "
                    "stickers, solid/dashed/dotted lines and endpoint editing, outlined/curved arrows, "
                    "move/resize/recolor, arrow endpoint rotation, delete, undo/redo, "
                    "annotated PNG export, settings dialog and persistence.\n");
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
        if (selfTest || app.smoke)
            writeTestReport(selfTest ? L"self-test-results.txt" : L"smoke-test-results.txt",
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
