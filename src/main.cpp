#include "graphics.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <array>
#include <string>
#include <stdexcept>

using namespace snip;
namespace
{
constexpr wchar_t MainClass[] = L"JackSnip.Main.1", OverlayClass[] = L"JackSnip.Capture.1",
                  SettingsClass[] = L"JackSnip.Settings.1";
constexpr UINT TrayMessage = WM_APP + 20, LaunchMessage = WM_APP + 21;
constexpr UINT CaptureTimer = 1, StatusTimer = 2, SmokeTimer = 3, CopyFlashTimer = 4,
               SizeRepeatTimer = 5;
constexpr UINT SizeRepeatDelay = 300, SizeRepeatInterval = 35;
constexpr float StatusHeight = 32, ScrollbarSize = 16;
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
    Eyedropper,
    TextTool,
    RectangleTool,
    TextBold,
    TextBox,
    TextSizeMenu,
    ProfessionalBorder,
    ProfessionalBlur,
    ProfessionalRounded,
    SamtecLogo,
    SaveLocation,
    CenterView,
    FullScreen,
    ToggleActions,
    ToggleTools,
    ToggleFormatting,
    ColorFirst = 1100,
    ShowEditor = 1200,
    CircleStyleMenu = 1300,
    ArrowStyleMenu,
    CheckStyleMenu,
    LineStyleMenu,
    StyleChoiceFirst = 1400,
    TextSizeFirst = 1500,
    TextEditControl = 1600,
    LogoStyleFirst = 1800
};
constexpr const wchar_t *LogoStyleNames[] = {L"S - White badge", L"S - Soft watermark",
    L"Tiger - White badge", L"Tiger - Soft watermark", L"Wordmark - White badge", L"Wordmark - Soft watermark"};
const std::array<Color, 8> Palette = {rgb(239, 68, 68),   rgb(249, 115, 22), rgb(250, 204, 21),
                                      rgb(34, 197, 94),   rgb(14, 165, 233), rgb(168, 85, 247),
                                      rgb(255, 255, 255), rgb(15, 23, 42)};
constexpr std::array<uint8_t, 8> StyleCounts = {1, 1, 3, 5, 6, 3, 4, 1};
constexpr int StyleChoiceStride = 8;
constexpr const wchar_t *ToolNames[] = {L"Select", L"Pen", L"Circle", L"Arrow", L"Check", L"Line",
                                       L"Rectangle", L"Text"};
constexpr std::array<int, 10> FontSizes = {12, 16, 20, 24, 32, 40, 48, 64, 96, 144};
constexpr int styleCommand(Tool tool, int style)
{
    return StyleChoiceFirst + (static_cast<int>(tool) - static_cast<int>(Tool::Circle)) * StyleChoiceStride + style;
}
struct Button
{
    Rect rect;
    int command;
    std::wstring label;
};
struct ShapeChoice
{
    Tool tool;
    uint8_t style;
    const wchar_t *label;
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
    HWND textEdit = nullptr;
    HFONT textEditFont = nullptr;
    HBRUSH textEditBackground = nullptr;
    bool textNew = false, syncingText = false;
    Annotation textBefore;
    HMENU shapeMenu = nullptr;
    Graphics graphics;
    ExportOptions exportOptions;
    Com<ID2D1HwndRenderTarget> target;
    Com<ID2D1Bitmap> displayBitmap;
    Bitmap previewImage;
    std::vector<Annotation> previewItems;
    ExportOptions previewOptions;
    int previewEditingText = -1;
    bool previewValid = false;
    Bitmap image, desktop, dimDesktop;
    Bitmap pickerImage;
    bool pickingColor = false;
    HCURSOR penCursor = nullptr;
    float penCursorDiameter = 0;
    Color penCursorColor = 0;
    Document document;
    Tool tool = Tool::Select;
    std::array<Color, 8> colors = {Palette[0], Palette[0], Palette[0],
                                   Palette[0], Palette[3], Palette[0], Palette[0], Ink};
    std::array<uint8_t, 8> styles{};
    Tool geometryTool = Tool::Circle;
    float fontSize = 24;
    bool textBold = false, textBox = false;
    float thickness = 4, dpi = 1;
    View view;
    bool fit = true, dirty = false, capturePending = false, exiting = false, tray = false,
         spaceDown = false;
    bool selecting = false, changed = false, smoke = false;
    bool toolPreferencesDirty = false;
    bool exportPreferencesDirty = false;
    ULONGLONG copyFlashStarted = 0;
    HMENU logoMenu = nullptr;
    HMENU professionalMenu = nullptr;
    unsigned collapsedRows = 0;
    bool layoutPreferencesDirty = false, fullScreen = false;
    bool horizontalVisible = false, verticalVisible = false;
    HWND horizontalScroll = nullptr, verticalScroll = nullptr;
    WINDOWPLACEMENT windowedPlacement{};
    LONG_PTR windowedStyle = 0;
    HMENU windowedMenu = nullptr;
    Drag drag = Drag::None;
    Point dragStart, panStart;
    Annotation before;
    int handle = -1, virtualX = 0, virtualY = 0;
    POINT selectionStart{}, selectionEnd{};
    std::wstring iniPath, savePath, saveFolder, status;
    WORD hotkey = MAKEWORD('S', HOTKEYF_CONTROL | HOTKEYF_ALT);
    int hotkeyId = 1;
    bool hotkeyRegistered = false;
    std::vector<Button> buttons;
    int hover = 0, pressed = 0;
    int sizeRepeatCommand = 0;
    bool sizeRepeated = false, sizeRepeatUndo = false;
    size_t tooltipCount = 0;
    UINT taskbarCreated = 0;
} app;

LRESULT CALLBACK mainProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK overlayProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK settingsProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK scrollbarProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void command(int id);
void stopSizeRepeat();
void startSnip(bool instant = false);
void hideEditorForCapture();
void openOverlay();
void refreshEditorCursor();
void finishTextEditing(bool cancel = false, bool selectAfter = true);
void syncTextEditor();
void beginTextEditing(Point point, int existing = -1);
void buildButtons();
void repaint()
{
    if (app.window)
    {
        InvalidateRect(app.window, nullptr, FALSE);
        refreshEditorCursor();
    }
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
float rowHeight(int row)
{
    constexpr float heights[] = {60, 63, 43};
    return (app.collapsedRows & (1U << row)) ? 20 : heights[row];
}
float rowTop(int row)
{
    float top = 0;
    for (int i = 0; i < row; ++i)
        top += rowHeight(i);
    return top;
}
float toolbarHeight() { return app.fullScreen ? 0 : rowTop(3); }
Rect workspaceRect()
{
    auto r = clientDips();
    return {0, toolbarHeight(), r.right, std::max(toolbarHeight(), r.bottom - StatusHeight)};
}
Rect canvasRect()
{
    auto r = workspaceRect();
    r.right = std::max(r.left, r.right - (app.verticalVisible ? ScrollbarSize : 0));
    r.bottom = std::max(r.top, r.bottom - (app.horizontalVisible ? ScrollbarSize : 0));
    return r;
}
bool hasImage()
{
    return !app.image.empty();
}
int previewPadding()
{
    return app.exportOptions.professionalBorder && app.exportOptions.professionalBlur ? 20 : 0;
}
void resetPreview()
{
    app.displayBitmap.reset();
    app.previewImage = {};
    app.previewItems.clear();
    app.previewValid = false;
}
const Bitmap &previewImage()
{
    const int editingText = app.textEdit ? app.document.selected : -1;
    if (!app.previewValid || app.previewOptions != app.exportOptions ||
        app.previewItems != app.document.items || app.previewEditingText != editingText)
    {
        auto image =
            app.graphics.exportImage(app.image, app.document.items, app.exportOptions, editingText);
        app.previewImage = std::move(image);
        app.previewItems = app.document.items;
        app.previewOptions = app.exportOptions;
        app.previewEditingText = editingText;
        app.previewValid = true;
        app.displayBitmap.reset();
    }
    return app.previewImage;
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
bool textMode()
{
    return app.tool == Tool::Text ||
           (selected() && app.document.items[app.document.selected].kind == Tool::Text);
}
void loadToolPreferences()
{
    app.collapsedRows = GetPrivateProfileIntW(L"Settings", L"CollapsedRows", 0, app.iniPath.c_str()) & 7U;
    app.layoutPreferencesDirty = false;
    wchar_t folder[32768]{};
    GetPrivateProfileStringW(L"Settings", L"SaveFolder", L"", folder, 32768, app.iniPath.c_str());
    app.saveFolder = folder;
    app.exportOptions.professionalBorder =
        GetPrivateProfileIntW(L"Settings", L"ProfessionalBorder", 0, app.iniPath.c_str()) != 0;
    app.exportOptions.professionalBlur =
        GetPrivateProfileIntW(L"Settings", L"ProfessionalBlur", 1, app.iniPath.c_str()) != 0;
    app.exportOptions.professionalRounded =
        GetPrivateProfileIntW(L"Settings", L"ProfessionalRounded", 1, app.iniPath.c_str()) != 0;
    app.exportOptions.samtecLogo =
        GetPrivateProfileIntW(L"Settings", L"SamtecLogo", 0, app.iniPath.c_str()) != 0;
    const UINT logoStyle =
        GetPrivateProfileIntW(L"Settings", L"SamtecLogoStyle", 0, app.iniPath.c_str());
    app.exportOptions.samtecStyle = logoStyle < 6 ? static_cast<uint8_t>(logoStyle) : 0;
    app.exportPreferencesDirty = false;
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
    const UINT fontSize = GetPrivateProfileIntW(L"ToolPreferences", L"TextFontSize", 24, app.iniPath.c_str());
    app.fontSize = static_cast<float>(std::clamp(fontSize, 8U, 144U));
    app.textBold = GetPrivateProfileIntW(L"ToolPreferences", L"TextBold", 0, app.iniPath.c_str()) != 0;
    app.textBox = GetPrivateProfileIntW(L"ToolPreferences", L"TextBox", 0, app.iniPath.c_str()) != 0;
    app.geometryTool = GetPrivateProfileIntW(L"ToolPreferences", L"GeometryTool",
        static_cast<int>(Tool::Circle), app.iniPath.c_str()) == static_cast<int>(Tool::Rectangle)
        ? Tool::Rectangle : Tool::Circle;
    app.toolPreferencesDirty = false;
}
bool saveToolPreferences()
{
    if (app.layoutPreferencesDirty)
    {
        if (!WritePrivateProfileStringW(L"Settings", L"CollapsedRows",
            std::to_wstring(app.collapsedRows).c_str(), app.iniPath.c_str()))
            return false;
        app.layoutPreferencesDirty = false;
    }
    if (app.exportPreferencesDirty)
    {
        if (!WritePrivateProfileStringW(L"Settings", L"ProfessionalBorder",
                                        app.exportOptions.professionalBorder ? L"1" : L"0",
                                        app.iniPath.c_str()) ||
            !WritePrivateProfileStringW(L"Settings", L"ProfessionalBlur",
                                        app.exportOptions.professionalBlur ? L"1" : L"0",
                                        app.iniPath.c_str()) ||
            !WritePrivateProfileStringW(L"Settings", L"ProfessionalRounded",
                                        app.exportOptions.professionalRounded ? L"1" : L"0",
                                        app.iniPath.c_str()) ||
            !WritePrivateProfileStringW(L"Settings", L"SamtecLogo",
                                        app.exportOptions.samtecLogo ? L"1" : L"0",
                                        app.iniPath.c_str()) ||
            !WritePrivateProfileStringW(L"Settings", L"SamtecLogoStyle",
                                        std::to_wstring(app.exportOptions.samtecStyle).c_str(),
                                        app.iniPath.c_str()))
            return false;
        app.exportPreferencesDirty = false;
    }
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
    auto preference = [&](const wchar_t *key, int value) {
        section += std::wstring(key) + L"=" + std::to_wstring(value);
        section.push_back(L'\0');
    };
    preference(L"TextFontSize", static_cast<int>(app.fontSize));
    preference(L"TextBold", app.textBold);
    preference(L"TextBox", app.textBox);
    preference(L"GeometryTool", static_cast<int>(app.geometryTool));
    section.push_back(L'\0');
    if (!WritePrivateProfileSectionW(L"ToolPreferences", section.c_str(), app.iniPath.c_str()))
        return false;
    app.toolPreferencesDirty = false;
    return true;
}
void saveToolPreferencesOrNotify()
{
    if (!saveToolPreferences())
        error(app.window, "Preferences could not be saved. Keep Snipper in a writable folder.");
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
void releaseImage()
{
    stopSizeRepeat();
    KillTimer(app.window, CopyFlashTimer);
    app.copyFlashStarted = 0;
    finishTextEditing(true);
    app.pickingColor = false;
    app.pickerImage = {};
    app.document.clear();
    app.image = {};
    resetPreview();
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
    const auto workspace = workspaceRect();
    const int padding = previewPadding();
    const float w = (app.image.width + padding * 2) * app.view.scale,
                h = (app.image.height + padding * 2) * app.view.scale;
    app.horizontalVisible = app.verticalVisible = false;
    if (hasImage() && !app.fit)
    {
        // One scrollbar can reduce the other axis enough to require both.
        for (int i = 0; i < 2; ++i)
        {
            auto r = canvasRect();
            app.horizontalVisible = app.horizontalVisible || w > r.width() + .01f;
            app.verticalVisible = app.verticalVisible || h > r.height() + .01f;
        }
    }
    auto r = canvasRect();
    float width = std::max(1.0f, r.width() - 40), height = std::max(1.0f, r.height() - 40);
    if (hasImage() && app.fit)
    {
        app.view.scale =
            std::max(.0001f, std::min({width / (app.image.width + padding * 2),
                                       height / (app.image.height + padding * 2), 1 / app.dpi}));
        app.view.origin = {(r.width() - app.image.width * app.view.scale) / 2,
                           r.top + (r.height() - app.image.height * app.view.scale) / 2};
    }
    else if (hasImage())
    {
        // Keep oversized images within their scrollable bounds, and fitting images
        // entirely inside the workspace without creating artificial scroll ranges.
        const float inset = padding * app.view.scale;
        app.view.origin.x = std::clamp(app.view.origin.x, std::min(r.left, r.right - w) + inset,
                                       std::max(r.left, r.right - w) + inset);
        app.view.origin.y = std::clamp(app.view.origin.y, std::min(r.top, r.bottom - h) + inset,
                                       std::max(r.top, r.bottom - h) + inset);
    }
    auto scrollbar = [&](HWND control, bool visible, bool horizontal) {
        if (!control)
            return;
        if (visible)
        {
            const float page = std::max(1.0f, horizontal ? r.width() : r.height());
            const float extent = horizontal ? w : h;
            const float start = horizontal ? r.left : r.top;
            const float origin =
                (horizontal ? app.view.origin.x : app.view.origin.y) - padding * app.view.scale;
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            info.nMax = static_cast<int>(std::ceil(extent * 100)) - 1;
            info.nPage = static_cast<UINT>(std::round(page * 100));
            info.nPos = static_cast<int>(std::round((start - origin) * 100));
            SetScrollInfo(control, SB_CTL, &info, TRUE);
            const Rect bounds = horizontal ? Rect{r.left, r.bottom, r.right, workspace.bottom}
                                           : Rect{r.right, r.top, workspace.right, r.bottom};
            MoveWindow(control, static_cast<int>(bounds.left * app.dpi),
                       static_cast<int>(bounds.top * app.dpi),
                       static_cast<int>(bounds.width() * app.dpi),
                       static_cast<int>(bounds.height() * app.dpi), TRUE);
            InvalidateRect(control, nullptr, FALSE);
        }
        if (bool(IsWindowVisible(control)) != visible)
            ShowWindow(control, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    };
    scrollbar(app.horizontalScroll, app.horizontalVisible, true);
    scrollbar(app.verticalScroll, app.verticalVisible, false);
}
void centerView()
{
    // Re-evaluate the viewport after reserving space for any required scrollbars.
    for (int i = 0; i < 2; ++i)
    {
        updateView();
        const auto r = canvasRect();
        app.view.origin = {r.left + (r.width() - app.image.width * app.view.scale) / 2,
                           r.top + (r.height() - app.image.height * app.view.scale) / 2};
    }
    updateView();
    repaint();
}
void toggleFullScreen()
{
    finishTextEditing();
    if (!app.fullScreen)
    {
        app.windowedPlacement.length = sizeof(app.windowedPlacement);
        GetWindowPlacement(app.window, &app.windowedPlacement);
        app.windowedStyle = GetWindowLongPtrW(app.window, GWL_STYLE);
        app.windowedMenu = GetMenu(app.window);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST), &monitor))
            throw std::runtime_error("Cannot find the display for full screen.");
        app.fullScreen = true;
        SetMenu(app.window, nullptr);
        SetWindowLongPtrW(app.window, GWL_STYLE,
                          app.windowedStyle & ~(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_MINIMIZE));
        SetWindowPos(app.window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left,
                     monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED);
    }
    else
    {
        app.fullScreen = false;
        SetWindowLongPtrW(app.window, GWL_STYLE, app.windowedStyle);
        SetMenu(app.window, app.windowedMenu);
        SetWindowPlacement(app.window, &app.windowedPlacement);
        SetWindowPos(app.window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    centerView();
    buildButtons();
}
std::optional<Rect> centerButtonRect()
{
    if (!hasImage() || app.fit)
        return {};
    const auto r = canvasRect();
    const float w = app.image.width * app.view.scale, h = app.image.height * app.view.scale;
    const Point ideal{r.left + (r.width() - w) / 2, r.top + (r.height() - h) / 2};
    if (length(app.view.origin - ideal) < 3)
        return {};
    const float margin = 12 + previewPadding() * app.view.scale;
    const Rect image{app.view.origin.x - margin, app.view.origin.y - margin,
                     app.view.origin.x + w + margin, app.view.origin.y + h + margin};
    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    const Rect candidates[] = {{cx - 64, r.bottom - 44, cx + 64, r.bottom - 12},
                               {cx - 64, r.top + 12, cx + 64, r.top + 44},
                               {r.left + 12, cy - 16, r.left + 140, cy + 16},
                               {r.right - 140, cy - 16, r.right - 12, cy + 16}};
    for (auto candidate : candidates)
        if (candidate.left >= r.left && candidate.top >= r.top && candidate.right <= r.right &&
            candidate.bottom <= r.bottom &&
            (candidate.right <= image.left || candidate.left >= image.right ||
             candidate.bottom <= image.top || candidate.top >= image.bottom))
            return candidate;
    return {};
}
Point limited(Point p)
{
    return {std::clamp(p.x, 0.0f, static_cast<float>(app.image.width)),
            std::clamp(p.y, 0.0f, static_cast<float>(app.image.height))};
}
Bitmap penCursorPixels(float diameter, Color value)
{
    // The colored disk matches the physical stroke width. Two outer contrast rings keep
    // white/black brushes visible; the hotspot stays at the center of the stroke.
    diameter = std::max(1.0f, diameter);
    const int extent = static_cast<int>(std::ceil(diameter)) + 6;
    auto bitmap = Bitmap::create(extent, extent);
    const float center = extent / 2 + .5f, radius = diameter / 2;
    for (int y = 0; y < extent; ++y)
        for (int x = 0; x < extent; ++x)
        {
            const float distance = std::hypot(x + .5f - center, y + .5f - center);
            // Blend the edges of three nested disks for smooth fractional sizes.
            const float outer = std::clamp(radius + 2.5f - distance, 0.0f, 1.0f);
            const float black = std::clamp(radius + 1.5f - distance, 0.0f, 1.0f);
            const float fill = std::clamp(radius + .5f - distance, 0.0f, 1.0f);
            const size_t offset = (static_cast<size_t>(y) * extent + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
            {
                const unsigned component = (value >> ((2 - channel) * 8)) & 255;
                bitmap.pixels[offset + channel] = static_cast<uint8_t>(
                    std::round(255 * (outer - black) + component * fill));
            }
            bitmap.pixels[offset + 3] = static_cast<uint8_t>(std::round(255 * outer));
        }
    return bitmap;
}
HCURSOR currentPenCursor()
{
    const float diameter = std::max(1.0f, app.thickness * app.view.scale * app.dpi);
    const Color value = app.colors[static_cast<size_t>(Tool::Pen)];
    if (app.penCursor && std::abs(diameter - app.penCursorDiameter) < .001f &&
        value == app.penCursorColor)
        return app.penCursor;
    const auto pixels = penCursorPixels(diameter, value);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = pixels.width;
    info.bmiHeader.biHeight = -pixels.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const std::vector<uint8_t> maskBits(((pixels.width + 15) / 16) * 2 * pixels.height, 0);
    HBITMAP mask = CreateBitmap(pixels.width, pixels.height, 1, 1, maskBits.data());
    HCURSOR cursor = nullptr;
    if (bitmap && bits && mask)
    {
        std::copy(pixels.pixels.begin(), pixels.pixels.end(), static_cast<uint8_t *>(bits));
        ICONINFO icon{};
        icon.xHotspot = pixels.width / 2;
        icon.yHotspot = pixels.height / 2;
        icon.hbmMask = mask;
        icon.hbmColor = bitmap;
        cursor = static_cast<HCURSOR>(CreateIconIndirect(&icon));
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (mask)
        DeleteObject(mask);
    if (!cursor)
        return LoadCursorW(nullptr, IDC_ARROW);
    HCURSOR previous = app.penCursor;
    app.penCursor = cursor;
    app.penCursorDiameter = diameter;
    app.penCursorColor = value;
    if (previous)
    {
        if (GetCursor() == previous)
            SetCursor(cursor);
        DestroyCursor(previous);
    }
    return cursor;
}
bool enabled(int id);
HCURSOR editorCursor(Point point)
{
    const bool onCanvas = hasImage() && canvasRect().contains(point);
    if (app.drag == Drag::Pan ||
        (onCanvas && app.spaceDown && (app.horizontalVisible || app.verticalVisible)))
        return LoadCursorW(nullptr, IDC_SIZEALL);
    const bool onButton = std::any_of(app.buttons.begin(), app.buttons.end(), [&](const Button &b) {
        return b.rect.contains(point) && enabled(b.command);
    });
    if (onButton)
        return LoadCursorW(nullptr, IDC_HAND);
    if (onCanvas && app.pickingColor)
        return LoadCursorW(nullptr, IDC_CROSS);
    if (onCanvas && app.tool == Tool::Text)
        return LoadCursorW(nullptr, IDC_IBEAM);
    if (onCanvas && app.tool == Tool::Pen)
        return currentPenCursor();
    return LoadCursorW(nullptr, onCanvas && app.tool != Tool::Select ? IDC_CROSS : IDC_ARROW);
}
void refreshEditorCursor()
{
    POINT point{};
    if (!GetCursorPos(&point) || WindowFromPoint(point) != app.window)
        return;
    ScreenToClient(app.window, &point);
    RECT client{};
    GetClientRect(app.window, &client);
    if (!PtInRect(&client, point))
        return;
    updateView();
    SetCursor(editorCursor({point.x / app.dpi, point.y / app.dpi}));
}
void testPenCursor()
{
    for (float dpi : {1.0f, 1.5f, 2.0f})
        for (float zoom : {.1f, 1.0f, 8.0f})
            for (float thickness : {1.0f, 4.0f, 40.0f})
                for (Color value : {rgb(12, 34, 56), rgb(255, 255, 255), rgb(0, 0, 0)})
                {
                    app.dpi = dpi;
                    app.view.scale = zoom / dpi;
                    app.thickness = thickness;
                    app.colors[static_cast<size_t>(Tool::Pen)] = value;
                    const auto cursor = currentPenCursor();
                    const float diameter = std::max(1.0f, thickness * zoom);
                    const auto pixels = penCursorPixels(diameter, value);
                    ICONINFO info{};
                    if (!app.penCursor || cursor != app.penCursor || !GetIconInfo(cursor, &info))
                        throw std::runtime_error("Pen cursor creation failed.");
                    BITMAP bitmap{};
                    const bool dimensions = GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) &&
                        bitmap.bmWidth == pixels.width && bitmap.bmHeight == pixels.height &&
                        info.xHotspot == static_cast<DWORD>(pixels.width / 2) &&
                        info.yHotspot == static_cast<DWORD>(pixels.height / 2) && !info.fIcon;
                    DeleteObject(info.hbmColor);
                    DeleteObject(info.hbmMask);
                    const size_t center = (static_cast<size_t>(pixels.height / 2) * pixels.width +
                                           pixels.width / 2) * 4;
                    if (!dimensions || pixels.sample({static_cast<float>(pixels.width / 2),
                            static_cast<float>(pixels.height / 2)}) != value ||
                        pixels.pixels[center + 3] != 255 || pixels.pixels[3] != 0 ||
                        currentPenCursor() != cursor)
                        throw std::runtime_error("Pen cursor size, color, hotspot, or caching failed.");
                }
    DestroyCursor(app.penCursor);
    app.penCursor = nullptr;
    app.dpi = app.view.scale = 1;
    app.thickness = 4;
    app.colors[static_cast<size_t>(Tool::Pen)] = Palette[0];
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
    Rect draw, shapes, formatting;
};
ToolbarLayout toolbarLayout()
{
    const float width = clientDips().right;
    const float formattingWidth = textMode() ? 264 : 160;
    // Center the whole label/control group between the eyedropper and zoom controls.
    const float formattingLeft = (410 + width - 140 - formattingWidth) / 2;
    return {{20, rowTop(1) + 15, 310, rowTop(1) + 59},
            {width - 504, rowTop(1) + 15, width - 20, rowTop(1) + 59},
            {formattingLeft, rowTop(2) + 7, formattingLeft + formattingWidth, rowTop(2) + 35}};
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
        add(id, text, 84, rowTop(1) + 19);
        x -= 4;
        add(menu, L"", 24, rowTop(1) + 19);
        x += 8;
    };
    if (!app.fullScreen && !(app.collapsedRows & 1))
    {
        add(NewSnip, L"New snip", 142, 11);
        x = 194;
        add(Undo, L"", 36, 11);
        x += 8;
        add(Redo, L"", 36, 11);
        x = 296;
        add(FullScreen, L"", 36, 11);
        x = clientDips().right - 226;
        add(Copy, L"Copy", 114, 11);
        x += 6;
        add(Save, L"Save", 82, 11);
    }
    if (!app.fullScreen && !(app.collapsedRows & 2))
    {
        x = layout.draw.left + 8;
        add(SelectTool, L"Select", 90, rowTop(1) + 19);
        x += 8;
        add(PenTool, L"Pen", 80, rowTop(1) + 19);
        x += 8;
        add(TextTool, L"Text", 80, rowTop(1) + 19);
        x = layout.shapes.left + 8;
        addStyleTool(CircleTool, L"Shapes", CircleStyleMenu);
        addStyleTool(ArrowTool, L"Arrow", ArrowStyleMenu);
        addStyleTool(CheckTool, L"Check", CheckStyleMenu);
        addStyleTool(LineTool, L"Line", LineStyleMenu);
    }
    if (!app.fullScreen && !(app.collapsedRows & 4))
    {
        const float y = rowTop(2) + 7;
        x = 70;
        for (int i = 0; i < 8; ++i)
        {
            app.buttons.push_back({{x, y + 2, x + 24, y + 26}, ColorFirst + i, L""});
            x += 34;
        }
        x = 350;
        add(CustomColor, L"", 28, y, 28);
        add(Eyedropper, L"", 28, y, 28);
        x = layout.formatting.left + 52;
        add(SizeDown, L"\u2212", 28, y, 28);
        x += 48;
        add(SizeUp, L"+", 28, y, 28);
        if (textMode())
        {
            x = layout.formatting.left + 82;
            add(TextSizeMenu, L"", 46, y, 28);
            x = layout.formatting.left + 170;
            add(TextBold, L"B", 28, y, 28);
            x = layout.formatting.left + 204;
            add(TextBox, L"Box", 60, y, 28);
        }
        x = clientDips().right - 140;
        add(Fit, L"Fit", 48, y, 28);
        add(Actual, L"100%", 64, y, 28);
    }
    if (!app.fullScreen)
        for (int row = 0; row < 3; ++row)
        {
            x = clientDips().right - 19;
            const float height = std::min(24.0f, rowHeight(row) - 2);
            add(ToggleActions + row, L"", 18, rowTop(row) + (rowHeight(row) - height) / 2, height);
        }
    else
    {
        x = 12;
        const float y = clientDips().bottom - StatusHeight + 4;
        add(FullScreen, L"Exit full screen", 130, y, 24);
        add(Fit, L"Fit", 48, y, 24);
        add(Actual, L"100%", 64, y, 24);
    }
    if (hasImage())
    {
        x = clientDips().right - 282;
        add(CenterView, L"Center", 76, clientDips().bottom - StatusHeight + 4, 24);
        if (const auto rect = centerButtonRect())
            app.buttons.push_back({*rect, CenterView, L"Center image"});
    }
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
            case TextTool:
                hint = L"Text (T) - click anywhere and type";
                break;
            case TextBold:
                hint = L"Bold text (Ctrl+B)";
                break;
            case TextBox:
                hint = L"Put text in a rounded box";
                break;
            case TextSizeMenu:
                hint = L"Choose font size (px)";
                break;
            case CircleTool:
                hint = L"Circle or rectangle (O); dropdown shows shape previews; Shift makes a "
                       L"circle/square";
                break;
            case ArrowTool:
                hint = L"Arrow (A)";
                break;
            case CheckTool:
                hint = L"Check or X sticker (K); dropdown shows styles";
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
            case Eyedropper:
                hint = L"Pick a color from the image (I); Esc cancels";
                break;
            case SizeDown:
                hint = L"Smaller font or thinner stroke ([)";
                break;
            case SizeUp:
                hint = L"Larger font or thicker stroke (])";
                break;
            case Fit:
                hint = L"Fit image to the window";
                break;
            case Actual:
                hint = L"View at original size";
                break;
            case CenterView:
                hint = L"Center image without changing zoom";
                break;
            case FullScreen:
                hint = L"Full screen (F11); Esc returns to the editor";
                break;
            case ToggleActions:
                hint = (app.collapsedRows & 1) ? L"Expand actions" : L"Collapse actions";
                break;
            case ToggleTools:
                hint = (app.collapsedRows & 2) ? L"Expand tools and shapes"
                                               : L"Collapse tools and shapes";
                break;
            case ToggleFormatting:
                hint =
                    (app.collapsedRows & 4) ? L"Expand color and size" : L"Collapse color and size";
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
    if (id == Copy || id == Save || id == SaveAs || id == Fit || id == Actual || id == CenterView || id == Eyedropper ||
        id == TextTool || id == RectangleTool || id == TextBold || id == TextBox || id == TextSizeMenu ||
        (id >= SelectTool && id <= LineTool) || (id >= CircleStyleMenu && id <= LineStyleMenu))
        return hasImage();
    return true;
}
bool active(int id)
{
    if (id == TextTool)
        return app.tool == Tool::Text;
    if (id == CircleTool)
        return app.tool == Tool::Circle || app.tool == Tool::Rectangle;
    if (id == TextBold || id == TextBox)
    {
        const bool current = selected() && app.document.items[app.document.selected].kind == Tool::Text;
        return id == TextBold ? (current ? app.document.items[app.document.selected].bold : app.textBold)
                              : (current ? app.document.items[app.document.selected].boxed : app.textBox);
    }
    if (id == Eyedropper)
        return app.pickingColor;
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
    case TextTool:
        line(3, 4, 17, 4);
        line(10, 4, 10, 17);
        line(6, 17, 14, 17);
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
    case Eyedropper:
        line(12, 3, 17, 8);
        line(10, 5, 15, 10);
        line(13, 2, 18, 7);
        line(18, 7, 16, 9);
        line(13, 2, 11, 4);
        line(11, 6, 3, 14);
        line(3, 14, 2, 18);
        line(2, 18, 6, 17);
        line(6, 17, 14, 9);
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
    fill({0, client.bottom - StatusHeight, client.right, client.bottom}, rgb(255, 255, 255));
    fill({0, client.bottom - StatusHeight, client.right, client.bottom - StatusHeight + 1},
         rgb(231, 233, 241));
    fill({0, 0, client.right, toolbarHeight()}, rgb(255, 255, 255));
    if (!app.fullScreen)
        for (int row = 0; row < 3; ++row)
        {
            const float top = rowTop(row), bottom = top + rowHeight(row);
            if (row == 2 || (app.collapsedRows & (1U << row)))
                fill({0, top, client.right, bottom}, rgb(250, 250, 253));
            fill({0, bottom - 1, client.right, bottom}, rgb(227, 229, 238));
            if (app.collapsedRows & (1U << row))
            {
                const wchar_t *labels[] = {L"Actions", L"Tools & shapes", L"Color & size"};
                text(labels[row], {20, top, client.right - 24, bottom}, Muted,
                     app.graphics.smallFont.get());
            }
        }
    // The capture icon and action are one button; history uses the space it frees.
    if (!app.fullScreen && !(app.collapsedRows & 1))
    {
        divider(180, 19, 39);
        if (hasImage() && client.right > 960)
            text(std::to_wstring(app.image.width) + L" \u00D7 " + std::to_wstring(app.image.height),
                 {client.right - 365, 11, client.right - 238, 47}, Muted,
                 app.graphics.smallFont.get(), true);
    }
    const auto layout = toolbarLayout();
    const float toolTop = rowTop(1), formatTop = rowTop(2);
    if (!app.fullScreen && !(app.collapsedRows & 2))
    {
        text(L"DRAW", {layout.draw.left + 2, toolTop + 1, layout.draw.right, toolTop + 13}, Muted,
             app.graphics.labelFont.get());
        text(L"SHAPES", {layout.shapes.left + 2, toolTop + 1, layout.shapes.right, toolTop + 13},
             Muted, app.graphics.labelFont.get());
        divider((layout.draw.right + layout.shapes.left) / 2, toolTop + 17, toolTop + 57);
        panel(layout.draw, rgb(249, 250, 252), rgb(226, 229, 237));
        panel(layout.shapes, rgb(249, 248, 255), rgb(229, 225, 243));
    }
    if (!app.fullScreen && !(app.collapsedRows & 4))
    {
        text(L"Color", {22, formatTop + 5, 66, formatTop + 37}, Muted,
             app.graphics.smallFont.get());
        const float formattingLeft = layout.formatting.left;
        divider((410 + formattingLeft) / 2, formatTop + 11, formatTop + 31);
        text(textMode() ? L"Size" : L"Stroke",
             {formattingLeft, formatTop + 5, formattingLeft + 45, formatTop + 37}, Muted,
             app.graphics.smallFont.get());
        rounded({formattingLeft + 52, formatTop + 7, formattingLeft + 160, formatTop + 35},
                rgb(238, 239, 246), 8);
        divider((layout.formatting.right + client.right - 140) / 2, formatTop + 11, formatTop + 31);
        rounded({client.right - 140, formatTop + 7, client.right - 24, formatTop + 35},
                rgb(238, 239, 246), 8);
    }
    for (size_t index = 0; index < app.buttons.size(); ++index)
    {
        const auto &button = app.buttons[index];
        auto r = button.rect;
        bool on = active(button.command) ||
                  (button.command == CircleStyleMenu &&
                   (app.tool == Tool::Circle || app.tool == Tool::Rectangle)) ||
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
        const bool toolButton = (button.command >= SelectTool && button.command <= LineTool) ||
                                button.command == TextTool;
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
        if (button.command >= ToggleActions && button.command <= ToggleFormatting)
        {
            brush->SetColor(color(over ? Accent : Muted));
            const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            const float direction =
                (app.collapsedRows & (1U << (button.command - ToggleActions))) ? 1 : -1;
            rt->DrawLine({cx - 4, cy - direction * 2}, {cx, cy + direction * 2}, brush.get(), 1.6f);
            rt->DrawLine({cx, cy + direction * 2}, {cx + 4, cy - direction * 2}, brush.get(), 1.6f);
        }
        else if (button.command == FullScreen && !app.fullScreen)
        {
            brush->SetColor(color(fg));
            const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            for (float sx : {-1.0f, 1.0f})
                for (float sy : {-1.0f, 1.0f})
                {
                    rt->DrawLine({cx + sx * 3, cy + sy * 7}, {cx + sx * 7, cy + sy * 7},
                                 brush.get(), 1.5f);
                    rt->DrawLine({cx + sx * 7, cy + sy * 7}, {cx + sx * 7, cy + sy * 3},
                                 brush.get(), 1.5f);
                }
        }
        else if (styleMenu)
        {
            brush->SetColor(color(fg));
            float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            rt->DrawLine({cx - 3, cy - 1}, {cx, cy + 2}, brush.get(), 1.3f);
            rt->DrawLine({cx, cy + 2}, {cx + 3, cy - 1}, brush.get(), 1.3f);
        }
        else if (button.command >= CircleTool && button.command <= LineTool)
        {
            Annotation icon;
            icon.kind = button.command == CircleTool
                            ? app.geometryTool
                            : static_cast<Tool>(button.command - SelectTool);
            icon.color = button.command == CheckTool && available
                             ? app.colors[static_cast<size_t>(Tool::Check)] : fg;
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
                 button.command == SelectTool || button.command == PenTool ||
                 button.command == TextTool)
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
        else if (button.command == Undo || button.command == Redo ||
                 button.command == CustomColor || button.command == Eyedropper)
            drawUIIcon(rt, brush.get(), button.command,
                       {(r.left + r.right) / 2 - 10, (r.top + r.bottom) / 2 - 10}, fg);
        else if (button.command == TextSizeMenu)
        {
            const float size =
                selected() && app.document.items[app.document.selected].kind == Tool::Text
                    ? app.document.items[app.document.selected].fontSize
                    : app.fontSize;
            text(std::to_wstring(static_cast<int>(size)), r, fg, app.graphics.smallFont.get(),
                 true);
        }
        else
            text(button.label, r, fg, app.graphics.font.get(), true);
    }
    auto minus = std::find_if(app.buttons.begin(), app.buttons.end(),
                              [](const Button &b) { return b.command == SizeDown; });
    float size = selected() && app.document.items[app.document.selected].kind != Tool::Check
                     ? app.document.items[app.document.selected].thickness
                     : app.thickness;
    if (minus != app.buttons.end() && !textMode())
        text(std::to_wstring(static_cast<int>(size)) + L" px",
             {minus->rect.right + 2, minus->rect.top, minus->rect.right + 52, minus->rect.bottom},
             Ink, app.graphics.smallFont.get(), true);
    if (hasImage())
    {
        const auto &preview = previewImage();
        auto &display = alternate ? alternateBitmap : app.displayBitmap;
        if (!display)
        {
            auto pixels = preview.pixels;
            for (size_t i = 0; i < pixels.size(); i += 4)
                for (int channel = 0; channel < 3; ++channel)
                    pixels[i + channel] =
                        static_cast<uint8_t>((pixels[i + channel] * pixels[i + 3] + 127) / 255);
            check(rt->CreateBitmap(
                      D2D1::SizeU(preview.width, preview.height), pixels.data(), preview.width * 4,
                      D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                               D2D1_ALPHA_MODE_PREMULTIPLIED),
                                             96, 96),
                      display.put()),
                  "Cannot display the screenshot.");
        }
        rt->PushAxisAlignedClip({canvas.left, canvas.top, canvas.right, canvas.bottom},
                                D2D1_ANTIALIAS_MODE_ALIASED);
        const float padding = previewPadding() * app.view.scale;
        auto o = app.view.origin - Point{padding, padding};
        float w = preview.width * app.view.scale, h = preview.height * app.view.scale;
        rt->SetTransform(D2D1::Matrix3x2F::Scale(app.view.scale, app.view.scale) *
                         D2D1::Matrix3x2F::Translation(o.x, o.y));
        rt->DrawBitmap(display.get(),
                       D2D1::RectF(0, 0, static_cast<float>(preview.width),
                                   static_cast<float>(preview.height)),
                       1,
                       app.view.scale * app.dpi >= 1
                           ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                           : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        if (app.copyFlashStarted)
        {
            const float fade =
                std::max(0.0f, 1 - (GetTickCount64() - app.copyFlashStarted) / 280.0f);
            brush->SetColor(color(Accent, .12f * fade));
            rt->FillRectangle({o.x, o.y, o.x + w, o.y + h}, brush.get());
            brush->SetColor(color(Accent, .65f * fade));
            rt->DrawRectangle({o.x + 1, o.y + 1, o.x + w - 1, o.y + h - 1}, brush.get(), 2);
        }
        if (selected() && !app.textEdit && app.drag != Drag::Draw)
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
                L"Check / X: click to place; drag to size",
                L"Line: drag to draw; Shift snaps angle; drag endpoints to resize",
                L"Rectangle: drag to draw; Shift makes a square",
                L"Text: click and type; Ctrl+Enter finishes; double-click to edit"};
            message = app.pickingColor ? L"Eyedropper: click the image to pick a color; Esc cancels"
                      : app.textEdit ? L"Text: Ctrl+Enter finishes; Enter adds a line; Esc cancels"
                                     : hints[static_cast<int>(app.tool)];
        }
        else
            message = L"Ready when you are";
    }
    brush->SetColor(color(app.status.empty() ? Accent : rgb(42, 169, 106)));
    if (!app.fullScreen)
        rt->FillEllipse(D2D1::Ellipse({22, client.bottom - StatusHeight / 2}, 3, 3), brush.get());
    text(message,
         {app.fullScreen ? 282.0f : 34.0f, client.bottom - StatusHeight,
          client.right - (hasImage() ? 292 : 14), client.bottom},
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
        resetPreview();
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
    // Include the native scroll controls in editor QA images.
    for (HWND control : {app.horizontalScroll, app.verticalScroll})
    {
        if (!control || !IsWindowVisible(control))
            continue;
        RECT rect{};
        GetClientRect(control, &rect);
        POINT origin{};
        MapWindowPoints(control, app.window, &origin, 1);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = rect.right;
        info.bmiHeader.biHeight = -rect.bottom;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *bits = nullptr;
        HBITMAP surface = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC dc = CreateCompatibleDC(nullptr);
        if (!surface || !bits || !dc)
        {
            if (surface) DeleteObject(surface);
            if (dc) DeleteDC(dc);
            throw std::runtime_error("Cannot render native scrollbar preview.");
        }
        auto previous = SelectObject(dc, surface);
        SendMessageW(control, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
        GdiFlush();
        for (int y = 0; y < rect.bottom; ++y)
            for (int x = 0; x < rect.right; ++x)
                if (origin.x + x >= 0 && origin.y + y >= 0 && origin.x + x < result.width && origin.y + y < result.height)
                {
                    const auto src = static_cast<const uint8_t *>(bits) + (static_cast<size_t>(y) * rect.right + x) * 4;
                    const size_t out = (static_cast<size_t>(origin.y + y) * result.width + origin.x + x) * 4;
                    std::copy_n(src, 3, result.pixels.data() + out);
                    result.pixels[out + 3] = 255;
                }
        SelectObject(dc, previous);
        DeleteDC(dc);
        DeleteObject(surface);
    }
    return result;
}
LRESULT CALLBACK textEditProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                  UINT_PTR, DWORD_PTR)
{
    try
    {
        if (message == WM_CHAR && wp == 2)
            return 0; // Ctrl+B formats text; its translated control character is not content.
        if (message == WM_KEYDOWN)
        {
            if (wp == VK_F11)
            {
                command(FullScreen);
                return 0;
            }
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wp == VK_ESCAPE || (wp == VK_RETURN && ctrl))
            {
                finishTextEditing(wp == VK_ESCAPE);
                return 0;
            }
            if (ctrl && (wp == 'B' || wp == 'S' || wp == 'N'))
            {
                command(wp == 'B' ? TextBold : wp == 'N' ? NewSnip :
                    (GetKeyState(VK_SHIFT) & 0x8000) ? SaveAs : Save);
                return 0;
            }
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    catch (const std::exception &exception)
    {
        error(app.window, exception.what());
        return 0;
    }
}
void syncTextEditor()
{
    if (!app.textEdit || !selected() || app.syncingText)
        return;
    app.syncingText = true;
    struct SyncGuard
    {
        ~SyncGuard() { app.syncingText = false; }
    } guard;
    auto &item = app.document.items[app.document.selected];
    const float scale = app.view.scale * app.dpi;
    const float padding = item.boxed ? 12 : 0;
    Point origin = app.view.toScreen(item.a + Point{padding, padding});
    const auto canvas = canvasRect();
    const int x = static_cast<int>(origin.x * app.dpi), y = static_cast<int>(origin.y * app.dpi);
    const int availableWidth = std::max(1, static_cast<int>((canvas.right - origin.x) * app.dpi));
    const int availableHeight = std::max(1, static_cast<int>((canvas.bottom - origin.y) * app.dpi));
    HFONT font = CreateFontW(-std::max(1, static_cast<int>(std::round(item.fontSize * scale))),
        0, 0, 0, item.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
        app.graphics.annotationFontFamily.c_str());
    if (font)
    {
        SendMessageW(app.textEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        if (app.textEditFont)
            DeleteObject(app.textEditFont);
        app.textEditFont = font;
    }
    const int wrapWidth = std::max(1, static_cast<int>(std::ceil(item.textWidth * scale)));
    HDC measureDC = GetDC(app.textEdit);
    const auto previousFont = SelectObject(measureDC, app.textEditFont);
    RECT measured{0, 0, wrapWidth, 0};
    const std::wstring content = item.text.empty() ? L" " : item.text;
    DrawTextW(measureDC, content.c_str(), static_cast<int>(content.size()), &measured,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(measureDC, previousFont);
    ReleaseDC(app.textEdit, measureDC);
    const int contentWidth = static_cast<int>(std::ceil((item.b.x - item.a.x - padding * 2) * scale));
    const int width = std::min(availableWidth, std::max(24, std::max(contentWidth, int(measured.right)) + 4));
    const int height = std::min(availableHeight, std::max(24, std::max(int(measured.bottom),
        static_cast<int>(std::ceil((item.b.y - item.a.y - padding * 2) * scale))) + 4));

    // Paint the actual screenshot beneath the EDIT control. A pattern brush restores
    // those pixels on deletion/selection, unlike a hollow brush which leaves text trails.
    const auto backing = renderEditorPreview();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits)
    {
        if (bitmap)
            DeleteObject(bitmap);
        throw std::runtime_error("Cannot render inline text background.");
    }
    auto pixels = static_cast<uint8_t *>(bits);
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column)
        {
            const int sx = std::clamp(x + column, 0, backing.width - 1);
            const int sy = std::clamp(y + row, 0, backing.height - 1);
            const size_t src = (static_cast<size_t>(sy) * backing.width + sx) * 4;
            const size_t dst = (static_cast<size_t>(row) * width + column) * 4;
            std::copy_n(&backing.pixels[src], 4, &pixels[dst]);
        }
    HBRUSH background = CreatePatternBrush(bitmap);
    DeleteObject(bitmap);
    if (!background)
        throw std::runtime_error("Cannot paint inline text background.");
    if (app.textEditBackground)
        DeleteObject(app.textEditBackground);
    app.textEditBackground = background;
    SendMessageW(app.textEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SetWindowPos(app.textEdit, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
    // Keep wrapping independent of the auto-sized visible field.
    RECT format{0, 0, wrapWidth, height};
    SendMessageW(app.textEdit, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&format));
    ShowWindow(app.textEdit, app.pickingColor ? SW_HIDE : SW_SHOW);
    InvalidateRect(app.textEdit, nullptr, TRUE);
}
void updateTextFromEditor()
{
    if (!app.textEdit || !selected() || app.syncingText)
        return;
    const int length = GetWindowTextLengthW(app.textEdit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(app.textEdit, text.data(), length + 1);
    text.resize(length);
    auto &item = app.document.items[app.document.selected];
    item.text = std::move(text);
    app.graphics.measureText(item);
    syncTextEditor();
    repaint();
}
void finishTextEditing(bool cancel, bool selectAfter)
{
    if (!app.textEdit)
        return;
    if (app.sizeRepeatCommand)
    {
        stopSizeRepeat();
        app.pressed = 0;
        if (GetCapture() == app.window)
            ReleaseCapture();
    }
    // Read the control once more: multiline EDIT controls do not send EN_CHANGE for
    // every programmatic replacement, and committing must capture the visible content.
    if (!cancel)
        updateTextFromEditor();
    const int index = app.document.selected;
    HWND edit = std::exchange(app.textEdit, nullptr);
    DestroyWindow(edit);
    if (app.textEditBackground)
    {
        DeleteObject(app.textEditBackground);
        app.textEditBackground = nullptr;
    }
    if (app.textEditFont)
    {
        DeleteObject(app.textEditFont);
        app.textEditFont = nullptr;
    }
    if (selected())
    {
        const auto &item = app.document.items[index];
        const bool unchanged = !app.textNew && item.text == app.textBefore.text &&
            item.color == app.textBefore.color && item.fontSize == app.textBefore.fontSize &&
            item.bold == app.textBefore.bold && item.boxed == app.textBefore.boxed;
        if (cancel || (app.textNew && item.text.empty()) || unchanged)
        {
            app.document.cancel();
            if (!app.textNew)
                app.document.selected = index;
        }
        else
        {
            if (item.text.empty())
            {
                app.document.items.erase(app.document.items.begin() + index);
                app.document.selected = -1;
            }
            app.document.commit();
            app.dirty = true;
            updateTitle();
        }
    }
    if (selectAfter && !cancel)
        app.tool = Tool::Select;
    SetFocus(app.window);
    repaint();
}
void beginTextEditing(Point point, int existing)
{
    finishTextEditing();
    app.document.begin();
    app.textNew = existing < 0;
    if (app.textNew)
    {
        Annotation item;
        item.kind = Tool::Text;
        item.a = point;
        item.color = app.colors[static_cast<size_t>(Tool::Text)];
        item.fontSize = app.fontSize;
        item.bold = app.textBold;
        item.boxed = app.textBox;
        item.textWidth = std::max(1.0f, std::min(600.0f, app.image.width - point.x -
                                              (item.boxed ? 24 : 0)));
        app.graphics.measureText(item);
        app.document.items.push_back(std::move(item));
        existing = static_cast<int>(app.document.items.size()) - 1;
    }
    app.document.selected = existing;
    app.textBefore = app.document.items[existing];
    app.tool = Tool::Text;
    app.textEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 1, 1, app.window,
        reinterpret_cast<HMENU>(TextEditControl), app.instance, nullptr);
    if (!app.textEdit || !SetWindowSubclass(app.textEdit, textEditProcedure, 1, 0))
    {
        if (app.textEdit)
            DestroyWindow(std::exchange(app.textEdit, nullptr));
        app.document.cancel();
        throw std::runtime_error("Cannot open the inline text editor.");
    }
    SendMessageW(app.textEdit, EM_SETLIMITTEXT, 16384, 0);
    SetWindowTextW(app.textEdit, app.textBefore.text.c_str());
    syncTextEditor();
    SetFocus(app.textEdit);
    SendMessageW(app.textEdit, EM_SETSEL, app.textBefore.text.size(), app.textBefore.text.size());
    app.status.clear();
    repaint();
}
void changeTextFormatting(float size, bool bold, bool boxed)
{
    size = std::clamp(size, 8.0f, 144.0f);
    if (app.fontSize != size || app.textBold != bold || app.textBox != boxed)
        app.toolPreferencesDirty = true;
    app.fontSize = size;
    app.textBold = bold;
    app.textBox = boxed;
    if (selected() && app.document.items[app.document.selected].kind == Tool::Text)
    {
        auto &item = app.document.items[app.document.selected];
        if (item.fontSize == size && item.bold == bold && item.boxed == boxed)
        {
            if (app.textEdit)
                SetFocus(app.textEdit);
            repaint();
            return;
        }
        app.document.begin();
        item.fontSize = app.fontSize;
        item.bold = bold;
        item.boxed = boxed;
        app.graphics.measureText(item);
        if (!app.textEdit)
        {
            if (!app.sizeRepeatCommand)
                app.document.commit();
            app.dirty = true;
            updateTitle();
        }
        else
        {
            syncTextEditor();
            SetFocus(app.textEdit);
        }
    }
    repaint();
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
        if (!app.textEdit)
        {
            app.document.commit();
            app.dirty = true;
            updateTitle();
        }
    }
    if (app.textEdit)
    {
        syncTextEditor();
        SetFocus(app.textEdit);
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
    if (app.textEdit)
        SetFocus(app.textEdit);
}
void changeThickness(int delta)
{
    if (textMode())
    {
        const bool current = selected() && app.document.items[app.document.selected].kind == Tool::Text;
        const auto *item = current ? &app.document.items[app.document.selected] : nullptr;
        changeTextFormatting((item ? item->fontSize : app.fontSize) + delta,
            item ? item->bold : app.textBold, item ? item->boxed : app.textBox);
        return;
    }
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
        if (!app.sizeRepeatCommand)
            app.document.commit();
        app.dirty = true;
        updateTitle();
    }
    repaint();
}
void stopSizeRepeat()
{
    KillTimer(app.window, SizeRepeatTimer);
    app.sizeRepeatCommand = 0;
    app.sizeRepeated = false;
    if (app.sizeRepeatUndo)
        app.document.commit();
    app.sizeRepeatUndo = false;
}
void repeatSize()
{
    if (!app.sizeRepeatCommand || !app.pressed || GetCapture() != app.window)
    {
        stopSizeRepeat();
        return;
    }
    SetTimer(app.window, SizeRepeatTimer, SizeRepeatInterval, nullptr);
    const size_t index = static_cast<size_t>(app.pressed - 1);
    if (index >= app.buttons.size() || app.buttons[index].command != app.sizeRepeatCommand ||
        app.hover != app.sizeRepeatCommand || !enabled(app.sizeRepeatCommand))
        return;
    const bool alreadyEditing = app.document.editing();
    command(app.sizeRepeatCommand);
    // A whole hold is one undoable resize; inline typing keeps its existing transaction.
    if (!alreadyEditing && app.document.editing() && !app.textEdit)
        app.sizeRepeatUndo = true;
    app.sizeRepeated = true;
}
void selectTool(Tool tool)
{
    finishTextEditing();
    app.tool = tool;
    app.pickingColor = false;
    app.pickerImage = {};
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
                    if (button.command == SizeDown || button.command == SizeUp)
                    {
                        app.sizeRepeatCommand = button.command;
                        app.sizeRepeated = false;
                        SetTimer(app.window, SizeRepeatTimer, SizeRepeatDelay, nullptr);
                    }
                    repaint();
                }
                return;
            }
        }
    }
    if (app.textEdit && !app.pickingColor && !middle && !app.spaceDown)
    {
        finishTextEditing();
        // Clicking away commits once and leaves the finished annotation selected.
        return;
    }
    if (!hasImage() || !canvasRect().contains(screen))
        return;
    if (middle || app.spaceDown)
    {
        updateView();
        if (!app.horizontalVisible && !app.verticalVisible)
            return;
        finishTextEditing();
        app.drag = Drag::Pan;
        app.dragStart = screen;
        app.panStart = app.view.origin;
        app.fit = false;
        SetCapture(app.window);
        return;
    }
    Point p = app.view.toImage(screen);
    if (app.pickingColor)
    {
        const float padding = static_cast<float>(previewPadding());
        if (const auto value = app.pickerImage.sample(p + Point{padding, padding}))
        {
            app.pickingColor = false;
            app.pickerImage = {};
            changeColor(*value);
            status(L"Color picked from image");
        }
        return;
    }
    if (app.tool == Tool::Text)
    {
        if (app.image.sample(p))
        {
            const int hit = app.document.hit(p, 0);
            beginTextEditing(p, hit >= 0 && app.document.items[hit].kind == Tool::Text ? hit : -1);
        }
        return;
    }
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
        const auto delta = screen - app.dragStart;
        if (app.horizontalVisible)
            app.view.origin.x = app.panStart.x + delta.x;
        if (app.verticalVisible)
            app.view.origin.y = app.panStart.y + delta.y;
        updateView();
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
        else if ((app.tool == Tool::Circle || app.tool == Tool::Rectangle) && (GetKeyState(VK_SHIFT) & 0x8000))
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
            if (item.kind == Tool::Check || item.kind == Tool::Text || (GetKeyState(VK_SHIFT) & 0x8000))
            {
                auto r = app.before.bounds();
                Point delta = p - opposite;
                float aspect = r.height() > .001f ? r.width() / r.height() : 1;
                float w = std::max(4.0f, std::abs(delta.x)), h = w / std::max(.01f, aspect);
                p = {opposite.x + (delta.x < 0 ? -w : w), opposite.y + (delta.y < 0 ? -h : h)};
            }
            auto to = rectangle(opposite, p);
            if (to.width() >= 2 && to.height() >= 2)
            {
                item.resize(app.before.bounds(), to);
                if (item.kind == Tool::Text)
                    app.graphics.measureText(item);
            }
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
        const bool repeated = app.sizeRepeated;
        stopSizeRepeat();
        app.pressed = 0;
        ReleaseCapture();
        Point screen{GET_X_LPARAM(lp) / app.dpi, GET_Y_LPARAM(lp) / app.dpi};
        if (!repeated && index < app.buttons.size() && app.buttons[index].rect.contains(screen) &&
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
    finishTextEditing();
    if (!hasImage())
        return;
    Point point = app.view.toImage(screen);
    app.fit = false;
    app.view.scale = std::clamp(app.view.scale * factor, .01f / app.dpi, 8 / app.dpi);
    app.view.origin = screen - point * app.view.scale;
    repaint();
}
Bitmap renderedExport()
{
    return app.graphics.exportImage(app.image, app.document.items, app.exportOptions);
}
void startCopyFeedback()
{
    if (!hasImage() || !IsWindowVisible(app.window))
        return;
    app.copyFlashStarted = GetTickCount64();
    SetTimer(app.window, CopyFlashTimer, 16, nullptr);
    repaint();
}
void copyImage()
{
    if (!hasImage())
        return;
    auto bitmap = renderedExport();
    auto png = app.graphics.png(bitmap);
    if (!copyBitmap(app.window, bitmap, png))
    {
        status(L"Clipboard is busy. Try Ctrl+C again.");
        return;
    }
    app.dirty = false;
    updateTitle();
    startCopyFeedback();
    status(L"Copied image and annotations - ready to paste");
}
bool existingFolder(const std::wstring &path)
{
    const DWORD attributes = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring initialSavePath(const std::wstring &path)
{
    if (!existingFolder(app.saveFolder))
        return path;
    // A full filename makes the explicit preference win over the dialog's recent folder.
    return (std::filesystem::path(app.saveFolder) / std::filesystem::path(path).filename()).wstring();
}
void setSaveFolder(const std::wstring &folder)
{
    if (!existingFolder(folder))
        throw std::runtime_error("Choose an existing folder for saved snips.");
    if (!WritePrivateProfileStringW(L"Settings", L"SaveFolder", folder.c_str(), app.iniPath.c_str()))
        throw std::runtime_error("The save location could not be remembered. Keep Snipper in a writable folder.");
    app.saveFolder = folder;
}
void chooseSaveFolder()
{
    Com<IFileOpenDialog> dialog;
    check(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IFileOpenDialog), reinterpret_cast<void **>(dialog.put())), "Cannot open the folder picker.");
    DWORD options = 0;
    check(dialog->GetOptions(&options), "Cannot read folder picker options.");
    check(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
        FOS_NOCHANGEDIR), "Cannot configure the folder picker.");
    check(dialog->SetTitle(L"Save location for snips"), "Cannot set the folder picker title.");
    check(dialog->SetOkButtonLabel(L"Use this folder"), "Cannot label the folder picker.");
    if (existingFolder(app.saveFolder))
    {
        Com<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(app.saveFolder.c_str(), nullptr,
            __uuidof(IShellItem), reinterpret_cast<void **>(folder.put()))))
            check(dialog->SetFolder(folder.get()), "Cannot select the current save folder.");
    }
    const HRESULT result = dialog->Show(IsWindowVisible(app.window) ? app.window : nullptr);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return;
    check(result, "Windows could not open the folder picker.");
    Com<IShellItem> selectedFolder;
    check(dialog->GetResult(selectedFolder.put()), "Cannot read the selected folder.");
    PWSTR name = nullptr;
    check(selectedFolder->GetDisplayName(SIGDN_FILESYSPATH, &name), "Cannot read the folder path.");
    const std::wstring folder = name;
    CoTaskMemFree(name);
    setSaveFolder(folder);
    status(L"Save location: " + app.saveFolder);
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
    const auto initialPath = initialSavePath(buffer);
    wcsncpy_s(buffer, initialPath.c_str(), _TRUNCATE);
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
    auto bitmap = renderedExport();
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
    AppendMenuW(menu, MF_STRING, SaveLocation, L"Save location...");
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
void drawShapeChoice(const DRAWITEMSTRUCT &draw)
{
    const auto &choice = *reinterpret_cast<const ShapeChoice *>(draw.itemData);
    app.graphics.initialize();
    Com<ID2D1DCRenderTarget> target;
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            app.dpi * 96, app.dpi * 96);
    check(app.graphics.factory->CreateDCRenderTarget(&properties, target.put()),
          "Cannot draw shape choices.");
    check(target->BindDC(draw.hDC, &draw.rcItem), "Cannot bind shape menu drawing.");
    Com<ID2D1SolidColorBrush> brush;
    check(target->CreateSolidColorBrush(color(Ink), brush.put()), "Cannot draw shape menu.");
    const float width = (draw.rcItem.right - draw.rcItem.left) / app.dpi;
    const float height = (draw.rcItem.bottom - draw.rcItem.top) / app.dpi;
    const bool hover = (draw.itemState & ODS_SELECTED) != 0;
    const bool chosen = (draw.itemState & ODS_CHECKED) != 0;
    target->BeginDraw();
    target->Clear(color(rgb(255, 255, 255)));
    brush->SetColor(color(hover ? rgb(233, 226, 255) : chosen ? rgb(246, 242, 255) : rgb(255, 255, 255)));
    target->FillRoundedRectangle(D2D1::RoundedRect({3, 2, width - 3, height - 2}, 7, 7), brush.get());
    Annotation icon;
    icon.kind = choice.tool;
    icon.style = choice.style;
    icon.color = app.colors[static_cast<size_t>(choice.tool)];
    if (choice.tool == Tool::Check &&
        (choice.style >= 3) != (app.styles[static_cast<size_t>(Tool::Check)] >= 3))
        icon.color = choice.style >= 3 ? Palette[0] : Palette[3];
    icon.thickness = 2;
    icon.a = {0, 0};
    icon.b = {34, choice.tool == Tool::Check ? 34.0f : 25.0f};
    if (choice.tool == Tool::Arrow || choice.tool == Tool::Line)
    {
        icon.a = {0, 25};
        icon.b = {38, 0};
    }
    const auto bounds = icon.bounds();
    const float scale = std::min(40 / (bounds.width() + 5), 32 / (bounds.height() + 5));
    target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(30 - (bounds.left + bounds.right) / 2 * scale,
                                    height / 2 - (bounds.top + bounds.bottom) / 2 * scale));
    app.graphics.drawAnnotations(target.get(), {icon});
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    brush->SetColor(color(chosen || hover ? Accent : Ink));
    app.graphics.font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    target->DrawText(choice.label, static_cast<UINT32>(wcslen(choice.label)),
        app.graphics.font.get(), {62, 0, width - 10, height}, brush.get());
    check(target->EndDraw(), "Cannot finish shape menu drawing.");
}
void drawLogoChoice(const DRAWITEMSTRUCT &draw)
{
    const auto style = static_cast<uint8_t>(draw.itemID - LogoStyleFirst);
    app.graphics.initialize();
    Com<ID2D1DCRenderTarget> target;
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), app.dpi * 96,
        app.dpi * 96);
    check(app.graphics.factory->CreateDCRenderTarget(&properties, target.put()),
          "Cannot draw logo choices.");
    check(target->BindDC(draw.hDC, &draw.rcItem), "Cannot bind logo menu drawing.");
    Com<ID2D1SolidColorBrush> brush;
    check(target->CreateSolidColorBrush(color(Ink), brush.put()), "Cannot draw logo menu.");
    auto badge = app.graphics.samtecBadge(style);
    for (size_t i = 0; i < badge.pixels.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            badge.pixels[i + c] =
                static_cast<uint8_t>((badge.pixels[i + c] * badge.pixels[i + 3] + 127) / 255);
    Com<ID2D1Bitmap> bitmap;
    check(target->CreateBitmap(D2D1::SizeU(badge.width, badge.height), badge.pixels.data(),
                               badge.width * 4,
                               D2D1::BitmapProperties(D2D1::PixelFormat(
                                   DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                               bitmap.put()),
          "Cannot show logo preview.");
    const bool selected = draw.itemState & ODS_SELECTED;
    const float width = (draw.rcItem.right - draw.rcItem.left) / app.dpi;
    const float height = (draw.rcItem.bottom - draw.rcItem.top) / app.dpi;
    target->BeginDraw();
    target->Clear(color(selected ? rgb(242, 238, 255) : rgb(255, 255, 255)));
    const float scale = std::min(82.0f / badge.width, 54.0f / badge.height);
    const float w = badge.width * scale, h = badge.height * scale;
    target->DrawBitmap(bitmap.get(),
                       {28 + (82 - w) / 2, (height - h) / 2, 28 + (82 + w) / 2, (height + h) / 2},
                       1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    brush->SetColor(color(style == app.exportOptions.samtecStyle ? Accent : Muted));
    target->DrawEllipse(D2D1::Ellipse({12, height / 2}, 4, 4), brush.get(), 1);
    if (style == app.exportOptions.samtecStyle)
        target->FillEllipse(D2D1::Ellipse({12, height / 2}, 2.5f, 2.5f), brush.get());
    brush->SetColor(color(selected ? Accent : Ink));
    app.graphics.font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    target->DrawText(LogoStyleNames[style], static_cast<UINT32>(wcslen(LogoStyleNames[style])),
                     app.graphics.font.get(), {126, 13, width - 8, 37}, brush.get());
    brush->SetColor(color(Muted));
    const wchar_t *description =
        style % 2 ? L"Faint mark, adapts to background" : L"Compact white background";
    app.graphics.smallFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    target->DrawText(description, static_cast<UINT32>(wcslen(description)),
                     app.graphics.smallFont.get(), {126, 36, width - 8, 59}, brush.get());
    check(target->EndDraw(), "Cannot finish logo menu drawing.");
}
void showShapeChoices(int id)
{
    std::vector<ShapeChoice> choices;
    if (id == CircleStyleMenu)
        choices = {{Tool::Circle, 0, L"Circle"}, {Tool::Circle, 1, L"Highlight circle"},
            {Tool::Circle, 2, L"Dashed circle"}, {Tool::Rectangle, 0, L"Square"},
            {Tool::Rectangle, 1, L"Rounded square"}, {Tool::Rectangle, 2, L"Highlight box"},
            {Tool::Rectangle, 3, L"Filled box"}};
    else if (id == ArrowStyleMenu)
        choices = {{Tool::Arrow, 0, L"Classic"}, {Tool::Arrow, 1, L"Outlined"},
            {Tool::Arrow, 2, L"Curved gloss"}, {Tool::Arrow, 3, L"Straight gloss"},
            {Tool::Arrow, 4, L"Block gloss"}};
    else if (id == CheckStyleMenu)
        choices = {{Tool::Check, 0, L"Boxed check"}, {Tool::Check, 1, L"Circle badge"},
            {Tool::Check, 2, L"Simple check"}, {Tool::Check, 3, L"Boxed X"},
            {Tool::Check, 4, L"Circle X badge"}, {Tool::Check, 5, L"Simple X"}};
    else
        choices = {{Tool::Line, 0, L"Solid"}, {Tool::Line, 1, L"Dashed"}, {Tool::Line, 2, L"Dotted"}};
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    app.shapeMenu = menu;
    for (const auto &choice : choices)
    {
        const bool chosen = app.styles[static_cast<size_t>(choice.tool)] == choice.style &&
            (id != CircleStyleMenu || app.geometryTool == choice.tool);
        AppendMenuW(menu, MF_OWNERDRAW | (chosen ? MF_CHECKED : 0),
            styleCommand(choice.tool, choice.style), reinterpret_cast<LPCWSTR>(&choice));
    }
    POINT anchor{};
    const auto button = std::find_if(app.buttons.begin(), app.buttons.end(),
        [&](const Button &b) { return b.command == id; });
    if (button != app.buttons.end())
        anchor = {static_cast<LONG>((button->rect.left - 84) * app.dpi),
                  static_cast<LONG>((button->rect.bottom + 4) * app.dpi)};
    ClientToScreen(app.window, &anchor);
    SetForegroundWindow(app.window);
    const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        anchor.x, anchor.y, 0, app.window, nullptr);
    app.shapeMenu = nullptr;
    DestroyMenu(menu);
    if (choice)
        command(choice);
}
void command(int id)
{
    if (app.sizeRepeatCommand && id != app.sizeRepeatCommand)
    {
        stopSizeRepeat();
        app.pressed = 0;
        if (GetCapture() == app.window)
            ReleaseCapture();
    }
    const bool textFormatting =
        id == TextBold || id == TextBox || id == TextSizeMenu || id == SizeDown || id == SizeUp ||
        id == CustomColor || id == Eyedropper || (id >= ColorFirst && id < ColorFirst + 8) ||
        (id >= TextSizeFirst && id < TextSizeFirst + static_cast<int>(FontSizes.size()));
    if (!textFormatting)
        finishTextEditing();
    if (app.drag != Drag::None)
        finishDrag();
    if (id != Eyedropper && app.pickingColor)
    {
        app.pickingColor = false;
        app.pickerImage = {};
        syncTextEditor();
        repaint();
    }
    if (id >= LogoStyleFirst && id < LogoStyleFirst + 6)
    {
        app.exportOptions.samtecStyle = static_cast<uint8_t>(id - LogoStyleFirst);
        app.exportOptions.samtecLogo = true;
        app.exportPreferencesDirty = true;
        if (hasImage())
        {
            app.dirty = true;
            updateTitle();
        }
        status(std::wstring(L"Samtec Logo: ") + LogoStyleNames[app.exportOptions.samtecStyle]);
        return;
    }
    if (id >= ColorFirst && id < ColorFirst + 8)
    {
        changeColor(Palette[id - ColorFirst]);
        return;
    }
    if (id >= SelectTool && id <= LineTool)
    {
        if (hasImage())
            selectTool(id == CircleTool ? app.geometryTool : static_cast<Tool>(id - SelectTool));
        return;
    }
    if (id >= CircleStyleMenu && id <= LineStyleMenu)
    {
        if (!hasImage())
            return;
        showShapeChoices(id);
        return;
    }
    if (id >= StyleChoiceFirst && id < StyleChoiceFirst + 5 * StyleChoiceStride)
    {
        const int option = id - StyleChoiceFirst;
        const Tool tool = static_cast<Tool>(static_cast<int>(Tool::Circle) + option / StyleChoiceStride);
        const size_t toolIndex = static_cast<size_t>(tool);
        const auto style = static_cast<uint8_t>(option % StyleChoiceStride);
        if (!hasImage() || style >= StyleCounts[toolIndex])
            return;
        if (app.styles[toolIndex] != style)
        {
            if (tool == Tool::Check && (app.styles[toolIndex] >= 3) != (style >= 3))
                app.colors[toolIndex] = style >= 3 ? Palette[0] : Palette[3];
            app.styles[toolIndex] = style;
            app.toolPreferencesDirty = true;
        }
        if (tool == Tool::Circle || tool == Tool::Rectangle)
        {
            app.geometryTool = tool;
            app.toolPreferencesDirty = true;
        }
        selectTool(tool);
        return;
    }
    if (id >= TextSizeFirst && id < TextSizeFirst + static_cast<int>(FontSizes.size()))
    {
        changeTextFormatting(static_cast<float>(FontSizes[id - TextSizeFirst]),
                             active(TextBold), active(TextBox));
        return;
    }
    switch (id)
    {
    case TextTool:
        if (hasImage())
            selectTool(Tool::Text);
        break;
    case RectangleTool:
        if (hasImage())
            command(styleCommand(Tool::Rectangle, app.styles[static_cast<size_t>(Tool::Rectangle)]));
        break;
    case TextBold:
    case TextBox:
    {
        const float size = selected() && app.document.items[app.document.selected].kind == Tool::Text
            ? app.document.items[app.document.selected].fontSize : app.fontSize;
        changeTextFormatting(size, id == TextBold ? !active(TextBold) : active(TextBold),
                             id == TextBox ? !active(TextBox) : active(TextBox));
        break;
    }
    case TextSizeMenu:
    {
        HMENU menu = CreatePopupMenu();
        if (!menu)
            break;
        const float size = selected() && app.document.items[app.document.selected].kind == Tool::Text
            ? app.document.items[app.document.selected].fontSize : app.fontSize;
        for (size_t i = 0; i < FontSizes.size(); ++i)
            AppendMenuW(menu, MF_STRING | (FontSizes[i] == size ? MF_CHECKED : 0),
                TextSizeFirst + i, (std::to_wstring(FontSizes[i]) + L" px").c_str());
        POINT point{};
        GetCursorPos(&point);
        const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            point.x, point.y, 0, app.window, nullptr);
        DestroyMenu(menu);
        if (choice)
            command(choice);
        else if (app.textEdit)
            SetFocus(app.textEdit);
        break;
    }
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
    case CenterView:
        if (hasImage())
            centerView();
        break;
    case FullScreen:
        toggleFullScreen();
        break;
    case ToggleActions:
    case ToggleTools:
    case ToggleFormatting:
    {
        const auto previous = canvasRect();
        app.collapsedRows ^= 1U << (id - ToggleActions);
        app.layoutPreferencesDirty = true;
        const auto next = canvasRect();
        if (!app.fit)
            app.view.origin.y += (next.top + next.bottom - previous.top - previous.bottom) / 2;
        updateView();
        buildButtons();
        repaint();
        break;
    }
    case Actual:
        if (hasImage())
        {
            app.fit = false;
            app.view.scale = 1 / app.dpi;
            centerView();
        }
        break;
    case CustomColor:
        customColor();
        break;
    case Eyedropper:
        if (hasImage())
        {
            if (app.pickingColor)
            {
                app.pickingColor = false;
                app.pickerImage = {};
            }
            else
            {
                app.pickerImage = app.graphics.exportImage(app.image, app.document.items, app.exportOptions);
                app.pickingColor = true;
            }
            app.status.clear();
            repaint();
            if (app.textEdit)
            {
                syncTextEditor();
                SetFocus(app.pickingColor ? app.window : app.textEdit);
            }
        }
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
    case SaveLocation:
        chooseSaveFolder();
        break;
    case Startup:
        toggleStartup();
        break;
    case ProfessionalBorder:
    case ProfessionalBlur:
    case ProfessionalRounded:
    case SamtecLogo: {
        auto &option = id == ProfessionalBorder    ? app.exportOptions.professionalBorder
                       : id == ProfessionalBlur    ? app.exportOptions.professionalBlur
                       : id == ProfessionalRounded ? app.exportOptions.professionalRounded
                                                   : app.exportOptions.samtecLogo;
        option = !option;
        if (id == ProfessionalBorder && option)
            app.exportOptions.professionalBlur = app.exportOptions.professionalRounded = true;
        app.exportPreferencesDirty = true;
        if (hasImage())
        {
            app.dirty = true;
            updateTitle();
        }
        const wchar_t *label = id == ProfessionalBorder    ? L"Professional Border"
                               : id == ProfessionalBlur    ? L"Blur"
                               : id == ProfessionalRounded ? L"Rounded corners"
                                                           : L"Samtec Logo";
        status(std::wstring(label) +
               (option ? L" enabled for copied and saved images" : L" disabled"));
        break;
    }
    case ShowEditor:
        showEditor();
        break;
    case About:
        MessageBoxW(
            app.window,
            L"Snipper 1.0.1\n\nNative C++ screenshot editor.\nDeveloped by Jack Kempf\n\nCtrl+N: new snip\nCtrl+C: "
            L"copy image with annotations\nCtrl+S: save PNG\nCtrl+Shift+S: Save As\nCtrl+Z "
            L"/ Ctrl+Y: undo / redo\nV / P / O / A / K / L: select / pen / circle / arrow / "
            L"check / line\n[ / ]: brush size\nDelete: remove selection\nCtrl+wheel: "
            L"zoom\nMiddle-drag or Space+drag: pan\nEsc: cancel capture or current "
            L"edit\n\nClose the window to stay in the tray.\nFile > Exit quits "
            L"completely.\n\nShortcut settings are saved beside the executable.",
            L"About Snipper", MB_OK | MB_ICONINFORMATION);
        break;
    case Exit:
        saveToolPreferencesOrNotify();
        app.exiting = true;
        DestroyWindow(app.window);
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
    // For hotkeys, defer SW_HIDE and focus changes until the desktop has been frozen.
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
    // Reserve the request so repeated snips cannot replace an in-progress capture.
    app.capturePending = true;
    try
    {
        if (instant)
        {
            cloakEditorForCapture();
            freezeDesktop();
        }
        finishTextEditing();
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
    AppendMenuW(view, MF_STRING, CenterView, L"&Center image");
    AppendMenuW(view, MF_STRING, FullScreen, L"&Full screen\tF11");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ToggleActions, L"&Actions row");
    AppendMenuW(view, MF_STRING, ToggleTools, L"&Tools and shapes row");
    AppendMenuW(view, MF_STRING, ToggleFormatting, L"Color and &size row");
    AppendMenuW(settings, MF_STRING, Settings, L"&Keyboard shortcut...");
    AppendMenuW(settings, MF_STRING, SaveLocation, L"Save &location...");
    AppendMenuW(settings, MF_STRING, Startup, L"Run at &sign-in");
    AppendMenuW(settings, MF_SEPARATOR, 0, nullptr);
    app.professionalMenu = CreatePopupMenu();
    AppendMenuW(app.professionalMenu, MF_STRING, ProfessionalBorder, L"&Enabled");
    AppendMenuW(app.professionalMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(app.professionalMenu, MF_STRING, ProfessionalBlur, L"&Blur");
    AppendMenuW(app.professionalMenu, MF_STRING, ProfessionalRounded, L"&Rounded corners");
    AppendMenuW(settings, MF_POPUP, reinterpret_cast<UINT_PTR>(app.professionalMenu),
                L"&Professional Border");
    app.logoMenu = CreatePopupMenu();
    AppendMenuW(app.logoMenu, MF_STRING, SamtecLogo, L"&Enabled");
    AppendMenuW(app.logoMenu, MF_SEPARATOR, 0, nullptr);
    for (int style = 0; style < 6; ++style)
    {
        AppendMenuW(app.logoMenu, MF_OWNERDRAW, LogoStyleFirst + style,
                    reinterpret_cast<LPCWSTR>(style + 1));
        MENUITEMINFOW label{};
        label.cbSize = sizeof(label);
        label.fMask = MIIM_STRING;
        label.dwTypeData = const_cast<LPWSTR>(LogoStyleNames[style]);
        SetMenuItemInfoW(app.logoMenu, LogoStyleFirst + style, FALSE, &label);
    }
    AppendMenuW(settings, MF_POPUP, reinterpret_cast<UINT_PTR>(app.logoMenu), L"Samtec &Logo");
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
    HMENU menu = app.fullScreen ? app.windowedMenu : GetMenu(app.window);
    for (int row = 0; row < 3; ++row)
        CheckMenuItem(menu, ToggleActions + row,
                      MF_BYCOMMAND |
                          ((app.collapsedRows & (1U << row)) ? MF_UNCHECKED : MF_CHECKED));
    CheckMenuItem(menu, FullScreen, MF_BYCOMMAND | (app.fullScreen ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ProfessionalBorder,
                  MF_BYCOMMAND |
                      (app.exportOptions.professionalBorder ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ProfessionalBlur,
                  MF_BYCOMMAND | (app.exportOptions.professionalBlur ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ProfessionalRounded,
                  MF_BYCOMMAND |
                      (app.exportOptions.professionalRounded ? MF_CHECKED : MF_UNCHECKED));
    for (int id : {ProfessionalBlur, ProfessionalRounded})
        EnableMenuItem(menu, id,
                       MF_BYCOMMAND |
                           (app.exportOptions.professionalBorder ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(menu, SamtecLogo,
                  MF_BYCOMMAND | (app.exportOptions.samtecLogo ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(app.logoMenu, LogoStyleFirst, LogoStyleFirst + 5,
                       LogoStyleFirst + app.exportOptions.samtecStyle, MF_BYCOMMAND);
    auto enable = [&](int id, bool yes) {
        EnableMenuItem(menu, id, MF_BYCOMMAND | (yes ? MF_ENABLED : MF_GRAYED));
    };
    for (int id : {Copy, Save, SaveAs, Fit, Actual, CenterView})
        enable(id, hasImage());
    enable(Undo, app.document.canUndo());
    enable(Redo, app.document.canRedo());
    enable(DeleteSelected, selected());
    enable(Clear, !app.document.items.empty());
    CheckMenuItem(menu, Startup, MF_BYCOMMAND | (startupEnabled() ? MF_CHECKED : MF_UNCHECKED));
}
void processKey(WPARAM key)
{
    if (key == VK_F11)
    {
        command(FullScreen);
        return;
    }
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
         shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (key == VK_ESCAPE)
    {
        if (app.textEdit && !app.pickingColor && !app.pressed)
        {
            finishTextEditing(true);
            return;
        }
        if (app.pressed)
        {
            stopSizeRepeat();
            app.pressed = 0;
            ReleaseCapture();
            repaint();
        }
        else if (app.pickingColor)
        {
            app.pickingColor = false;
            app.pickerImage = {};
            syncTextEditor();
            repaint();
            if (app.textEdit)
                SetFocus(app.textEdit);
        }
        else if (app.drag != Drag::None)
            finishDrag(true);
        else if (app.fullScreen)
            command(FullScreen);
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
        case 'B':
            if (textMode())
                command(TextBold);
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
    case 'T':
        command(TextTool);
        break;
    case 'R':
        command(RectangleTool);
        break;
    case 'I':
        command(Eyedropper);
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
        refreshEditorCursor();
        break;
    default:
        break;
    }
}
void paintScrollbar(HWND hwnd, HDC dc)
{
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const bool horizontal = hwnd == app.horizontalScroll;
    const int extent = horizontal ? rect.right : rect.bottom;
    const int width = horizontal ? rect.bottom : rect.right;
    HBRUSH background = CreateSolidBrush(RGB(238, 240, 247));
    FillRect(dc, &rect, background);
    DeleteObject(background);
    SCROLLBARINFO info{};
    info.cbSize = sizeof(info);
    if (GetScrollBarInfo(hwnd, OBJID_CLIENT, &info) && info.xyThumbBottom > info.xyThumbTop)
    {
        HBRUSH thumb =
            CreateSolidBrush(GetCapture() == hwnd ? RGB(126, 132, 156) : RGB(173, 179, 199));
        HGDIOBJ oldBrush = SelectObject(dc, thumb),
                oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        const int inset = std::max(2, static_cast<int>(3 * app.dpi));
        const RECT thumbRect =
            horizontal ? RECT{info.xyThumbTop, inset, info.xyThumbBottom, width - inset}
                       : RECT{inset, info.xyThumbTop, width - inset, info.xyThumbBottom};
        const int radius = static_cast<int>(8 * app.dpi);
        RoundRect(dc, thumbRect.left, thumbRect.top, thumbRect.right, thumbRect.bottom, radius,
                  radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(thumb);
    }
    HPEN pen =
        CreatePen(PS_SOLID, std::max(1, static_cast<int>(1.5f * app.dpi)), RGB(123, 128, 147));
    HGDIOBJ previous = SelectObject(dc, pen);
    const int center = width / 2, step = std::max(2, static_cast<int>(3 * app.dpi));
    for (int end = 0; end < 2; ++end)
    {
        const int position = end ? extent - width / 2 : width / 2;
        const int direction = end ? 1 : -1;
        const auto points =
            horizontal ? std::array<POINT, 3>{{{position - direction * step / 2, center - step},
                                               {position + direction * step / 2, center},
                                               {position - direction * step / 2, center + step}}}
                       : std::array<POINT, 3>{{{center - step, position - direction * step / 2},
                                               {center, position + direction * step / 2},
                                               {center + step, position - direction * step / 2}}};
        Polyline(dc, points.data(), 3);
    }
    SelectObject(dc, previous);
    DeleteObject(pen);
}
LRESULT CALLBACK scrollbarProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR,
                                    DWORD_PTR)
{
    if (message == WM_PAINT)
    {
        // Let the native control update its thumb/hit-test geometry before restyling it.
        const auto result = DefSubclassProc(hwnd, message, wp, lp);
        HDC dc = GetDC(hwnd);
        paintScrollbar(hwnd, dc);
        ReleaseDC(hwnd, dc);
        return result;
    }
    if (message == WM_PRINTCLIENT)
    {
        DefSubclassProc(hwnd, WM_PAINT, 0, 0);
        paintScrollbar(hwnd, reinterpret_cast<HDC>(wp));
        return 0;
    }
    const auto result = DefSubclassProc(hwnd, message, wp, lp);
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_CAPTURECHANGED)
        InvalidateRect(hwnd, nullptr, FALSE);
    return result;
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
        app.horizontalScroll = CreateWindowExW(0, L"SCROLLBAR", nullptr, WS_CHILD | SBS_HORZ,
            0, 0, 0, 0, hwnd, nullptr, app.instance, nullptr);
        app.verticalScroll = CreateWindowExW(0, L"SCROLLBAR", nullptr, WS_CHILD | SBS_VERT,
            0, 0, 0, 0, hwnd, nullptr, app.instance, nullptr);
        if (!app.horizontalScroll || !app.verticalScroll)
            throw std::runtime_error("Cannot create image scrollbars.");
        if (!SetWindowSubclass(app.horizontalScroll, scrollbarProcedure, 1, 0) ||
            !SetWindowSubclass(app.verticalScroll, scrollbarProcedure, 1, 0))
            throw std::runtime_error("Cannot style image scrollbars.");
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
        finishTextEditing();
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
    case WM_MEASUREITEM:
    {
        auto item = reinterpret_cast<MEASUREITEMSTRUCT *>(lp);
        if (item->CtlType == ODT_MENU)
        {
            const bool logo = item->itemID >= LogoStyleFirst && item->itemID < LogoStyleFirst + 6;
            item->itemWidth = static_cast<UINT>((logo ? 320 : 184) * app.dpi);
            item->itemHeight = static_cast<UINT>((logo ? 72 : 48) * app.dpi);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM:
    {
        const auto item = reinterpret_cast<const DRAWITEMSTRUCT *>(lp);
        if (item->CtlType == ODT_MENU && item->itemData)
        {
            if (item->itemID >= LogoStyleFirst && item->itemID < LogoStyleFirst + 6)
                drawLogoChoice(*item);
            else
                drawShapeChoice(*item);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lp) == app.textEdit && selected())
        {
            SetTextColor(reinterpret_cast<HDC>(wp), activeColor());
            if (app.textEditBackground)
            {
                SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
                SetBrushOrgEx(reinterpret_cast<HDC>(wp), 0, 0, nullptr);
                return reinterpret_cast<LRESULT>(app.textEditBackground);
            }
            const Color background = textBackground(activeColor());
            SetBkColor(reinterpret_cast<HDC>(wp), background);
            SetDCBrushColor(reinterpret_cast<HDC>(wp), background);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == TextEditControl)
        {
            if (HIWORD(wp) == EN_CHANGE)
                updateTextFromEditor();
            return 0;
        }
        command(LOWORD(wp));
        return 0;
    case WM_INITMENUPOPUP:
        updateMenus();
        return 0;
    case WM_LBUTTONDOWN:
        mouseDown(lp);
        return 0;
    case WM_LBUTTONDBLCLK:
    {
        Point screen{GET_X_LPARAM(lp) / app.dpi, GET_Y_LPARAM(lp) / app.dpi};
        if (hasImage() && canvasRect().contains(screen))
        {
            const Point point = app.view.toImage(screen);
            const int hit = app.document.hit(point, 0);
            if (hit >= 0 && app.document.items[hit].kind == Tool::Text)
            {
                finishDrag(true);
                beginTextEditing(point, hit);
                return 0;
            }
        }
        mouseDown(lp);
        return 0;
    }
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
        stopSizeRepeat();
        if (app.pressed)
        {
            app.pressed = 0;
            repaint();
        }
        if (app.drag != Drag::None)
            finishDrag(true);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE)
            break;
        [[fallthrough]];
    case WM_CANCELMODE:
        stopSizeRepeat();
        if (app.pressed)
        {
            app.pressed = 0;
            if (GetCapture() == hwnd)
                ReleaseCapture();
            repaint();
        }
        break;
    case WM_MOUSEWHEEL: {
        if (!hasImage())
            return 0;
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)
        {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            zoomAt({p.x / app.dpi, p.y / app.dpi},
                   GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.2f : 1 / 1.2f);
        }
        else
        {
            updateView();
            const bool horizontal = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;
            if (horizontal ? !app.horizontalVisible : !app.verticalVisible)
                return 0;
            finishTextEditing();
            app.fit = false;
            float amount = GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 48;
            if (horizontal)
                app.view.origin.x += amount;
            else
                app.view.origin.y += amount;
            updateView();
            repaint();
        }
        return 0;
    }
    case WM_HSCROLL:
    case WM_VSCROLL: {
        const bool horizontal = message == WM_HSCROLL;
        const HWND control = horizontal ? app.horizontalScroll : app.verticalScroll;
        if (!hasImage() || reinterpret_cast<HWND>(lp) != control)
            return 0;
        updateView();
        if (horizontal ? !app.horizontalVisible : !app.verticalVisible)
            return 0;
        finishTextEditing();
        app.fit = false;
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(control, SB_CTL, &info);
        int position = info.nPos;
        switch (LOWORD(wp))
        {
        case SB_LINEUP:
            position -= 4800;
            break;
        case SB_LINEDOWN:
            position += 4800;
            break;
        case SB_PAGEUP:
            position -= static_cast<int>(info.nPage * .85);
            break;
        case SB_PAGEDOWN:
            position += static_cast<int>(info.nPage * .85);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            position = info.nTrackPos;
            break;
        case SB_TOP:
            position = info.nMin;
            break;
        case SB_BOTTOM:
            position = info.nMax;
            break;
        default:
            return 0;
        }
        position = std::clamp(position, info.nMin,
                              std::max(info.nMin, info.nMax - static_cast<int>(info.nPage) + 1));
        const auto r = canvasRect();
        if (horizontal)
            app.view.origin.x = r.left + previewPadding() * app.view.scale - position / 100.0f;
        else
            app.view.origin.y = r.top + previewPadding() * app.view.scale - position / 100.0f;
        updateView();
        repaint();
        return 0;
    }
    case WM_KEYDOWN:
        processKey(wp);
        return 0;
    case WM_KEYUP:
        if (wp == VK_SPACE)
        {
            app.spaceDown = false;
            refreshEditorCursor();
        }
        return 0;
    case WM_KILLFOCUS:
        app.spaceDown = false;
        refreshEditorCursor();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
        {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            SetCursor(editorCursor({p.x / app.dpi, p.y / app.dpi}));
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
        if (wp == SizeRepeatTimer)
            repeatSize();
        else if (wp == CaptureTimer)
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
        else if (wp == CopyFlashTimer)
        {
            if (!app.copyFlashStarted || GetTickCount64() - app.copyFlashStarted >= 280)
            {
                KillTimer(hwnd, CopyFlashTimer);
                app.copyFlashStarted = 0;
            }
            repaint();
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
            command(Exit);
        }
        return 0;
    case WM_CLOSE:
        if (app.settingsWindow)
            closeSettings();
        finishTextEditing(true);
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
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp)
            saveToolPreferences();
        return 0;
    case WM_DESTROY:
        stopSizeRepeat();
        finishTextEditing(true);
        if (app.penCursor)
        {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            DestroyCursor(app.penCursor);
            app.penCursor = nullptr;
        }
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
            resetPreview();
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
    cls.style = CS_DBLCLKS;
    cls.hInstance = app.instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(app.instance, MAKEINTRESOURCEW(101));
    cls.hIconSm = cls.hIcon;
    cls.lpszClassName = MainClass;
    cls.lpfnWndProc = mainProcedure;
    if (!RegisterClassExW(&cls))
        throw std::runtime_error("Cannot register the editor window.");
    cls.style = 0;
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
class SmokeNoPromptGuard
{
    HHOOK hook = nullptr;
    static LRESULT CALLBACK observe(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HCBT_ACTIVATE)
        {
            HWND window = reinterpret_cast<HWND>(wp);
            wchar_t name[80]{}, title[80]{};
            GetClassNameW(window, name, 80);
            GetWindowTextW(window, title, 80);
            if (wcscmp(name, L"#32770") == 0 && wcscmp(title, L"Snipper") == 0 &&
                GetDlgItem(window, IDNO))
            {
                shown = true;
                // Prevent a regression from hanging the automated test in a modal prompt.
                PostMessageW(window, WM_COMMAND, IDNO, 0);
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

  public:
    static inline bool shown = false;
    explicit SmokeNoPromptGuard(bool enabled)
    {
        if (enabled)
        {
            shown = false;
            hook = SetWindowsHookExW(WH_CBT, observe, nullptr, GetCurrentThreadId());
            if (!hook)
                throw std::runtime_error("Cannot observe unexpected save confirmations.");
        }
    }
    ~SmokeNoPromptGuard()
    {
        if (hook)
            UnhookWindowsHookEx(hook);
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
            diagnostic += "; process " + std::to_string(foregroundProcess) + "; test process " +
                          std::to_string(GetCurrentProcessId());
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
const std::array<Color, 8> PersistenceTestColors = {
    Palette[0],      rgb(12, 34, 56),  rgb(210, 87, 133), rgb(15, 120, 220),
    rgb(8, 91, 200), rgb(90, 35, 170), rgb(18, 90, 140),  rgb(25, 50, 75)};
const std::array<uint8_t, 8> PersistenceTestStyles = {0, 0, 1, 4, 5, 1, 1, 0};
std::wstring smokeMenuPreviewPath;
std::string smokeMenuPreviewError;
void CALLBACK smokeShapeMenuTimer(HWND hwnd, UINT, UINT_PTR id, DWORD)
{
    KillTimer(hwnd, id);
    try
    {
        RECT first{}, last{};
        const bool logo = app.shapeMenu == app.logoMenu;
        const bool professional = app.shapeMenu == app.professionalMenu;
        const int count = GetMenuItemCount(app.shapeMenu);
        if (GetMenuItemID(app.shapeMenu, 0) == static_cast<UINT>(styleCommand(Tool::Check, 0)))
        {
            if (count != StyleCounts[static_cast<size_t>(Tool::Check)])
                throw std::runtime_error("Check/X menu is missing styles.");
            for (int style = 0; style < count; ++style)
                if (GetMenuItemID(app.shapeMenu, style) != static_cast<UINT>(styleCommand(Tool::Check, style)))
                    throw std::runtime_error("Check/X menu command or order is incorrect.");
        }
        if (count < 3 || !GetMenuItemRect(hwnd, app.shapeMenu, logo ? 2 : 0, &first) ||
            !GetMenuItemRect(hwnd, app.shapeMenu, count - 1, &last) ||
            first.right - first.left < (logo ? 320 : professional ? 96 : 184) * app.dpi ||
            first.bottom - first.top < (logo ? 72 : professional ? 18 : 48) * app.dpi)
            throw std::runtime_error("Visual shape menu entries are missing or too small.");
        const auto pixels = captureDesktop(first.left, first.top, first.right - first.left,
                                          last.bottom - first.top);
        saveBytes(smokeMenuPreviewPath, app.graphics.png(pixels));
    }
    catch (const std::exception &exception)
    {
        smokeMenuPreviewError = exception.what();
    }
    EndMenu();
}
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
                app.tool != Tool::Select || app.toolPreferencesDirty || app.fontSize != 40 ||
                !app.textBold || !app.textBox || app.geometryTool != Tool::Rectangle ||
                !app.exportOptions.professionalBorder || !app.exportOptions.samtecLogo || app.exportPreferencesDirty ||
                app.exportOptions.professionalBlur || !app.exportOptions.professionalRounded ||
                app.saveFolder != (std::filesystem::path(testDirectory) / L"smoke-save location").wstring() ||
                app.collapsedRows != 6 || app.layoutPreferencesDirty || app.fullScreen || app.exportOptions.samtecStyle != 5)
                throw std::runtime_error("Tool preferences did not survive a complete process exit.");
            writeTestReport(L"preference-test-results.txt",
                "PASS: a fresh process restored every tool's style and custom color after full exit; "
                "Professional Border with separate blur/rounding, Samtec Logo and selected style, save location, and collapsed rows restored, active tool unchanged, no pending preference write.\n");
        }
        else if (selfTest)
        {
            app.graphics.test();
            testPenCursor();
            writeTestReport(L"self-test-results.txt",
                            "PASS: model history, cancellation, hit testing, resizing, coordinate "
                            "transforms, cropping, pen/circle/arrow/check composition, six check/X styles at small and large sizes, "
                            "solid/dashed/dotted line patterns, outlined/curved/straight/block gloss arrow artwork and "
                            "selection, PNG "
                            "pixel-perfect round trip, moved annotations, image color sampling, "
                            "text/bold/multiline/rounded-box export, square/rounded/highlight/filled rectangles, "
                            "native pen cursor color/size/hotspot at 100/150/200% DPI and 10/100/800% zoom; "
                            "Professional Border default OFF, independent blur/rounding, 20px transparent padding with blur, softened neutral halo on white/black without a hard outline, "
                            "unchanged content, tiny snips, transparent PNG round trip.\n");
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
                if (app.exportOptions.professionalBorder || app.exportOptions.samtecLogo || app.exportPreferencesDirty)
                    throw std::runtime_error("Professional Border must start OFF without saved preferences.");
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
            SmokeNoPromptGuard noPrompts(app.smoke);
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
                UpdateWindow(window);
                command(NewSnip);
                if (SmokeNoPromptGuard::shown || IsWindowVisible(window) || !app.capturePending)
                    throw std::runtime_error("New snip prompted instead of immediately hiding the unsaved editor.");
                SendMessageW(window, WM_HOTKEY, app.hotkeyId, 0);
                if (!app.capturePending || app.overlay)
                    throw std::runtime_error("Repeated shortcut replaced a pending capture.");
                SendMessageW(window, WM_TIMER, CaptureTimer, 0);
                if (!app.overlay)
                    throw std::runtime_error("Snip did not create its selection overlay.");
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
                // Its pixels must already be frozen before an overlay activates.
                for (int mode = 0; mode < 4; ++mode)
                {
                    app.document.clear();
                    app.dirty = (mode & 1) != 0;
                    if (app.dirty)
                        app.document.items.push_back(unsaved);
                    if (mode >= 2)
                        ShowWindow(window, SW_HIDE);
                    SmokeHoverPopup popup(editorBounds.left + 40, editorBounds.top + 100);
                    const auto reference = captureDesktop(popup.bounds.left, popup.bounds.top,
                        popup.bounds.right - popup.bounds.left, popup.bounds.bottom - popup.bounds.top);
                    SendMessageW(window, WM_HOTKEY, app.hotkeyId, 0);
                    if (!popup.dismissed)
                        throw std::runtime_error("Hover-menu fixture did not dismiss on focus loss.");
                    {
                        if (SmokeNoPromptGuard::shown || !app.overlay || app.capturePending)
                            throw std::runtime_error("Hotkey capture still waits for a capture timer.");
                        const HWND firstOverlay = app.overlay;
                        SendMessageW(window, WM_HOTKEY, app.hotkeyId, 0);
                        if (app.overlay != firstOverlay)
                            throw std::runtime_error("Repeated shortcut replaced active selection.");
                        const auto frozen = app.desktop.crop(popup.bounds.left - app.virtualX,
                            popup.bounds.top - app.virtualY, reference.width, reference.height);
                        if (frozen.pixels != reference.pixels)
                            throw std::runtime_error("Instant capture lost the hover popup before freezing the screen.");
                        SendMessageW(app.overlay, WM_KEYDOWN, VK_ESCAPE, 0);
                        if (app.overlay || !app.desktop.empty() || !IsWindowVisible(window) ||
                            app.image.width != 180 || app.image.height != 120)
                            throw std::runtime_error("Canceling instant selection failed to restore the previous snip.");
                        if ((mode & 1) && (!app.dirty || app.document.items.size() != 1))
                            throw std::runtime_error("Canceling selection lost previous unsaved annotations.");
                    }
                    SendMessageW(window, WM_TIMER, CaptureTimer, 0);
                    if (app.overlay || app.capturePending || !app.desktop.empty())
                        throw std::runtime_error("A stale capture timer reopened canceled instant selection.");
                }
                app.document.clear();
                app.dirty = false;
                resetPreview();
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
                auto sizeButtonPoint = [&](int id) {
                    buildButtons();
                    const auto button =
                        std::find_if(app.buttons.begin(), app.buttons.end(),
                                     [&](const Button &b) { return b.command == id; });
                    if (button == app.buttons.end())
                        throw std::runtime_error("Size button is missing.");
                    return MAKELPARAM(
                        static_cast<int>((button->rect.left + button->rect.right) / 2 * app.dpi),
                        static_cast<int>((button->rect.top + button->rect.bottom) / 2 * app.dpi));
                };
                auto sizeValue = [&]() {
                    if (selected())
                    {
                        const auto &item = app.document.items[app.document.selected];
                        return item.kind == Tool::Text ? item.fontSize : item.thickness;
                    }
                    return textMode() ? app.fontSize : app.thickness;
                };
                auto startSizeHold = [&](int id) {
                    const auto point = sizeButtonPoint(id);
                    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
                    if (app.sizeRepeatCommand != id || !app.pressed || GetCapture() != window)
                        throw std::runtime_error(
                            "Holding a size button did not arm repeat/capture.");
                    return point;
                };
                auto sizeTicks = [&](int count) {
                    for (int tick = 0; tick < count; ++tick)
                        SendMessageW(window, WM_TIMER, SizeRepeatTimer, 0);
                };
                auto endSizeHold = [&](LPARAM point) {
                    SendMessageW(window, WM_LBUTTONUP, 0, point);
                    const float value = sizeValue();
                    sizeTicks(2); // Queued ticks must not change size after release.
                    if (app.sizeRepeatCommand || app.sizeRepeated || app.sizeRepeatUndo ||
                        app.pressed || GetCapture() == window || sizeValue() != value)
                        throw std::runtime_error(
                            "Size repeat continued after releasing the button.");
                };
                const float originalBrushSize = app.thickness, originalFontSize = app.fontSize;
                for (Tool tool : {Tool::Pen, Tool::Text})
                {
                    selectTool(tool);
                    clickButton(SizeUp, true);
                    const float beforeClick = sizeValue();
                    clickButton(SizeUp);
                    if (sizeValue() != beforeClick + 1)
                        throw std::runtime_error(
                            "A quick size click should change exactly one pixel.");
                    for (int id : {SizeUp, SizeDown})
                    {
                        const float beforeHold = sizeValue();
                        const float delta = id == SizeUp ? 1 : -1;
                        const auto point = startSizeHold(id);
                        if (sizeValue() != beforeHold)
                            throw std::runtime_error("Size repeat skipped its initial delay.");
                        sizeTicks(5);
                        if (sizeValue() != beforeHold + delta * 5)
                            throw std::runtime_error(
                                "Holding +/- did not smoothly repeat font/brush sizing.");
                        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(2, 2));
                        sizeTicks(2);
                        if (sizeValue() != beforeHold + delta * 5)
                            throw std::runtime_error(
                                "Size repeat did not pause outside the button.");
                        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, point);
                        sizeTicks(1);
                        endSizeHold(point);
                        if (sizeValue() != beforeHold + delta * 6)
                            throw std::runtime_error(
                                "Size repeat failed to resume or added an extra release step.");
                    }
                    for (UINT cancel : {WM_CANCELMODE, WM_CAPTURECHANGED, WM_ACTIVATE, WM_KEYDOWN})
                    {
                        startSizeHold(SizeUp);
                        sizeTicks(1);
                        if (cancel == WM_CAPTURECHANGED)
                            ReleaseCapture();
                        else
                            SendMessageW(window, cancel, cancel == WM_KEYDOWN ? VK_ESCAPE : 0, 0);
                        const float value = sizeValue();
                        sizeTicks(2);
                        if (app.pressed || app.sizeRepeatCommand || sizeValue() != value)
                            throw std::runtime_error("Cancelled size hold kept repeating.");
                    }
                    if (tool == Tool::Pen)
                        app.thickness = 39;
                    else
                        app.fontSize = 143;
                    auto point = startSizeHold(SizeUp);
                    sizeTicks(4);
                    endSizeHold(point);
                    if (sizeValue() != (tool == Tool::Pen ? 40 : 144))
                        throw std::runtime_error("Size hold exceeded the maximum.");
                    if (tool == Tool::Pen)
                        app.thickness = 2;
                    else
                        app.fontSize = 9;
                    point = startSizeHold(SizeDown);
                    sizeTicks(4);
                    endSizeHold(point);
                    if (sizeValue() != (tool == Tool::Pen ? 1 : 8))
                        throw std::runtime_error("Size hold exceeded the minimum.");
                }
                for (Tool kind : {Tool::Pen, Tool::Text})
                {
                    app.document.clear();
                    Annotation item;
                    item.kind = kind;
                    item.a = {20, 20};
                    item.b = {100, 80};
                    item.points = {item.a, item.b};
                    item.text = L"Hold to resize";
                    if (kind == Tool::Text)
                        app.graphics.measureText(item);
                    app.document.items.push_back(item);
                    app.document.selected = 0;
                    app.tool = Tool::Select;
                    const float originalSize = sizeValue();
                    const auto point = startSizeHold(SizeUp);
                    sizeTicks(7);
                    if (sizeValue() != originalSize + 7 || !app.document.editing())
                        throw std::runtime_error(
                            "Held resize did not update a selected annotation live.");
                    endSizeHold(point);
                    if (app.document.editing() || !app.document.undo() || app.document.canUndo() ||
                        (kind == Tool::Text ? app.document.items[0].fontSize
                                            : app.document.items[0].thickness) != originalSize)
                        throw std::runtime_error("A held resize should undo as a single change.");
                }
                app.document.clear();
                app.thickness = originalBrushSize;
                app.fontSize = originalFontSize;
                selectTool(Tool::Pen);
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
                // The picker uses exported image pixels, independent of zoom, DPI, and chrome.
                const Color penColor = app.colors[static_cast<size_t>(Tool::Pen)];
                clickButton(Eyedropper);
                if (!app.pickingColor || !active(Eyedropper))
                    throw std::runtime_error("Eyedropper toolbar did not activate.");
                SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON,
                    MAKELPARAM(2, static_cast<int>((canvasRect().top + 2) * app.dpi)));
                SendMessageW(window, WM_LBUTTONUP, 0,
                    MAKELPARAM(2, static_cast<int>((canvasRect().top + 2) * app.dpi)));
                if (!app.pickingColor || activeColor() != penColor || app.document.items.size() != 1)
                    throw std::runtime_error("Eyedropper sampled workspace pixels or drew an annotation.");
                processKey(VK_ESCAPE);
                if (app.pickingColor || app.tool != Tool::Pen || activeColor() != penColor)
                    throw std::runtime_error("Canceling the eyedropper changed the previous tool/color.");
                for (float scale : {.5f, 1.0f, 2.0f})
                {
                    app.fit = false;
                    app.view.scale = scale / app.dpi;
                    app.view.origin = {30, canvasRect().top + 20};
                    const Point sample{200, 80};
                    const Point screen = app.view.toScreen(sample);
                    const LPARAM click = MAKELPARAM(static_cast<int>(screen.x * app.dpi),
                                                     static_cast<int>(screen.y * app.dpi));
                    const Point actual = app.view.toImage({GET_X_LPARAM(click) / app.dpi,
                                                          GET_Y_LPARAM(click) / app.dpi});
                    const auto expected = app.image.sample(actual);
                    processKey('I');
                    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, click);
                    SendMessageW(window, WM_LBUTTONUP, 0, click);
                    if (!expected || app.pickingColor || app.tool != Tool::Pen ||
                        activeColor() != *expected || app.document.items.size() != 1 ||
                        app.drag != Drag::None || GetCapture() == window)
                        throw std::runtime_error("Eyedropper sampling at different zoom levels failed.");
                    const auto cursor = editorCursor(screen);
                    if (cursor != app.penCursor || app.penCursorColor != *expected ||
                        std::abs(app.penCursorDiameter - std::max(1.0f, app.thickness * scale)) > .001f)
                        throw std::runtime_error("Picked color or zoom did not update the pen cursor.");
                }
                changeColor(penColor);
                command(Fit);
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
                // Sample an existing pen stroke into the selected shape, then undo the recolor.
                const int circleSelection = app.document.selected;
                clickButton(Eyedropper);
                dragImage({100, 50}, {100, 50});
                if (app.pickingColor || app.document.selected != circleSelection ||
                    app.document.items[1].color != penColor ||
                    app.colors[static_cast<size_t>(Tool::Circle)] != penColor)
                    throw std::runtime_error("Eyedropper did not sample annotations/recolor the selection.");
                command(Undo);
                if (app.document.items[1].color != Palette[4])
                    throw std::runtime_error("Eyedropper recolor was not undoable.");
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
                for (int style = 3; style < 6; ++style)
                {
                    command(styleCommand(Tool::Check, style));
                    dragImage({600, 120}, {600, 120});
                    auto bounds = app.document.items.back().bounds();
                    dragImage({bounds.right, bounds.bottom}, {bounds.right + 20, bounds.bottom + 20});
                    bounds = app.document.items.back().bounds();
                    dragImage({(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2},
                              {(bounds.left + bounds.right) / 2 - 30, (bounds.top + bounds.bottom) / 2 - 20});
                    const auto &cross = app.document.items.back();
                    if (cross.kind != Tool::Check || cross.style != style || cross.color != Palette[0] ||
                        cross.bounds().width() < 70 || app.tool != Tool::Select ||
                        !cross.hit({(cross.a.x + cross.b.x) / 2, (cross.a.y + cross.b.y) / 2}, 1))
                        throw std::runtime_error("Red X click placement, move, resize, or selection failed.");
                    command(DeleteSelected);
                    command(Undo);
                    if (app.document.items.back().style != style)
                        throw std::runtime_error("Undo red X deletion lost its style.");
                    command(Redo);
                    if (app.document.items.size() != 4)
                        throw std::runtime_error("Redo red X deletion failed.");
                    command(styleCommand(Tool::Check, style));
                    dragImage({450, 20}, {520, 90});
                    if (app.document.items.back().style != style || app.document.items.back().color != Palette[0] ||
                        std::abs(app.document.items.back().bounds().width() - 70) > 2)
                        throw std::runtime_error("Red X drag placement failed.");
                    command(DeleteSelected);
                }
                command(styleCommand(Tool::Check, 0));
                if (activeColor() != Palette[3])
                    throw std::runtime_error("Switching back to checks did not choose green.");
                changeColor(Palette[4]);
                command(styleCommand(Tool::Check, 1));
                if (activeColor() != Palette[4])
                    throw std::runtime_error("Changing check badge lost its custom color.");
                command(styleCommand(Tool::Check, 3));
                if (activeColor() != Palette[0])
                    throw std::runtime_error("Switching to X did not choose red.");
                changeColor(Palette[5]);
                command(styleCommand(Tool::Check, 4));
                if (activeColor() != Palette[5])
                    throw std::runtime_error("Changing X badge lost its custom color.");
                command(styleCommand(Tool::Check, 0));
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
                for (int style = 1; style <= 4; ++style)
                {
                    command(styleCommand(Tool::Arrow, style));
                    dragImage({400, 190.0f + style * 20}, {550, 220.0f + style * 20});
                    const auto &arrow = app.document.items.back();
                    if (arrow.kind != Tool::Arrow || arrow.style != style ||
                        !arrow.hit(arrow.arrowSpine(.5f), 0))
                        throw std::runtime_error(
                            "Outlined, curved, straight, or block gloss arrow placement/selection failed.");
                }
                const size_t beforeText = app.document.items.size();
                clickButton(TextTool);
                dragImage({20, 200}, {20, 200});
                SendMessageW(app.textEdit, WM_CHAR, 'X', 0);
                dragImage({600, 40}, {600, 40});
                if (app.textEdit || app.document.editing() || app.tool != Tool::Select ||
                    app.document.items.size() != beforeText + 1 || app.document.items.back().text != L"X")
                    throw std::runtime_error("Clicking outside typed text did not commit and return to Select.");
                command(Undo);
                if (app.document.items.size() != beforeText)
                    throw std::runtime_error("Click-away text creation was not a single undoable edit.");
                clickButton(TextTool);
                dragImage({250, 270}, {250, 270});
                if (!app.textEdit || GetFocus() != app.textEdit || !app.document.editing())
                    throw std::runtime_error("Click-to-type did not open and focus inline text editing.");
                HDC fontDC = GetDC(app.textEdit);
                HGDIOBJ previousFont = SelectObject(fontDC, app.textEditFont);
                wchar_t nativeFamily[LF_FACESIZE]{};
                GetTextFaceW(fontDC, LF_FACESIZE, nativeFamily);
                SelectObject(fontDC, previousFont);
                ReleaseDC(app.textEdit, fontDC);
                if (app.graphics.annotationFontFamily != nativeFamily)
                    throw std::runtime_error("Typing and export use different annotation fonts.");
                RECT emptyField{};
                GetClientRect(app.textEdit, &emptyField);
                if (emptyField.right > 32 || (GetWindowLongPtrW(app.textEdit, GWL_STYLE) & WS_BORDER))
                    throw std::runtime_error("Empty inline text still shows a wide bordered input bar.");
                auto verifyInline = [&](const wchar_t *path) {
                    RECT field{};
                    GetClientRect(app.textEdit, &field);
                    POINT origin{};
                    MapWindowPoints(app.textEdit, window, &origin, 1);
                    auto scene = renderEditorPreview();
                    auto native = Bitmap::create(field.right, field.bottom);
                    BITMAPINFO info{};
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth = native.width;
                    info.bmiHeader.biHeight = -native.height;
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biBitCount = 32;
                    info.bmiHeader.biCompression = BI_RGB;
                    void *bits = nullptr;
                    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
                    HDC dc = CreateCompatibleDC(nullptr);
                    if (!bitmap || !bits || !dc)
                    {
                        if (bitmap) DeleteObject(bitmap);
                        if (dc) DeleteDC(dc);
                        throw std::runtime_error("Cannot capture native inline editor.");
                    }
                    HGDIOBJ previous = SelectObject(dc, bitmap);
                    SendMessageW(app.textEdit, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc),
                                 PRF_CLIENT | PRF_ERASEBKGND);
                    GdiFlush();
                    std::copy_n(static_cast<const uint8_t *>(bits), native.pixels.size(), native.pixels.begin());
                    SelectObject(dc, previous);
                    DeleteDC(dc);
                    DeleteObject(bitmap);
                    const auto actual = native.sample({float(native.width - 1), float(native.height - 1)});
                    const auto expected = scene.sample({float(origin.x + native.width - 1), float(origin.y + native.height - 1)});
                    if (actual != expected)
                    {
                        for (size_t i = 3; i < native.pixels.size(); i += 4)
                            native.pixels[i] = 255;
                        saveBytes(L"smoke-test-inline-failure.png", app.graphics.png(native));
                        throw std::runtime_error("Inline editor background mismatch: actual=" +
                            std::to_string(actual.value_or(0)) +
                            " expected=" + std::to_string(expected.value_or(0)));
                    }
                    for (int y = 0; y < native.height; ++y)
                        for (int x = 0; x < native.width; ++x)
                            if (origin.x + x >= 0 && origin.x + x < scene.width &&
                                origin.y + y >= 0 && origin.y + y < scene.height)
                            {
                                const size_t src = (static_cast<size_t>(y) * native.width + x) * 4;
                                const size_t dst = (static_cast<size_t>(origin.y + y) * scene.width + origin.x + x) * 4;
                                std::copy_n(&native.pixels[src], 3, &scene.pixels[dst]);
                            }
                    saveBytes(path, app.graphics.png(scene));
                };
                for (wchar_t character : std::wstring(L"Review this value"))
                    SendMessageW(app.textEdit, WM_CHAR, character, 0);
                RECT shortField{};
                GetClientRect(app.textEdit, &shortField);
                if (shortField.right <= emptyField.right || shortField.right > 240 ||
                    SendMessageW(app.textEdit, EM_GETLINECOUNT, 0, 0) != 1)
                    throw std::runtime_error("Inline editor did not grow with its text or wrapped prematurely.");
                SendMessageW(app.textEdit, WM_CHAR, VK_BACK, 0);
                RECT deletedField{};
                GetClientRect(app.textEdit, &deletedField);
                if (deletedField.right >= shortField.right)
                    throw std::runtime_error("Inline editor did not shrink when deleting text.");
                SendMessageW(app.textEdit, WM_CHAR, 'e', 0);
                verifyInline(L"smoke-test-inline-plain.png");
                clickButton(TextBold);
                clickButton(TextBox);
                command(TextSizeFirst + 4);
                clickButton(ColorFirst + 4);
                if (!app.textEdit || GetFocus() != app.textEdit ||
                    app.document.items.back().text != L"Review this value" ||
                    !app.document.items.back().bold || !app.document.items.back().boxed ||
                    app.document.items.back().fontSize != 32 || app.document.items.back().color != Palette[4])
                    throw std::runtime_error("Inline text typing/font/bold/box/color controls failed.");
                verifyInline(L"smoke-test-inline-boxed.png");
                for (int id : {SizeUp, SizeDown})
                {
                    const auto point = startSizeHold(id);
                    sizeTicks(3);
                    if (!app.textEdit || !app.document.editing() || app.sizeRepeatUndo ||
                        GetFocus() != app.textEdit ||
                        app.document.items.back().fontSize != (id == SizeUp ? 35 : 32))
                        throw std::runtime_error(
                            "Held font sizing interrupted inline typing or its undo transaction.");
                    endSizeHold(point);
                }
                clickButton(Eyedropper);
                if (!app.pickingColor || IsWindowVisible(app.textEdit))
                    throw std::runtime_error("Text editing obstructed image color picking.");
                processKey(VK_ESCAPE);
                if (!app.textEdit || !IsWindowVisible(app.textEdit) || GetFocus() != app.textEdit ||
                    !app.document.editing() ||
                    app.document.items.back().text != L"Review this value")
                    throw std::runtime_error(
                        "Canceling image picking discarded the inline text draft.");
                BYTE keyboard[256]{};
                GetKeyboardState(keyboard);
                BYTE ctrlKeyboard[256];
                std::copy(std::begin(keyboard), std::end(keyboard), std::begin(ctrlKeyboard));
                ctrlKeyboard[VK_CONTROL] |= 0x80;
                SetKeyboardState(ctrlKeyboard);
                SendMessageW(app.textEdit, WM_KEYDOWN, 'B', 0);
                SendMessageW(app.textEdit, WM_CHAR, 2, 0);
                const bool unbolded = !app.document.items.back().bold;
                SendMessageW(app.textEdit, WM_KEYDOWN, 'B', 0);
                SendMessageW(app.textEdit, WM_CHAR, 2, 0);
                const bool boldShortcut = unbolded && app.document.items.back().bold &&
                    app.document.items.back().text == L"Review this value";
                SendMessageW(app.textEdit, WM_KEYDOWN, VK_RETURN, 0);
                SetKeyboardState(keyboard);
                if (!boldShortcut || app.textEdit || app.document.editing() || app.tool != Tool::Select ||
                    app.document.items.size() != beforeText + 1)
                    throw std::runtime_error("Ctrl+Enter did not commit the text annotation.");
                command(Undo);
                if (app.document.items.size() != beforeText)
                    throw std::runtime_error("Text creation was not one undoable edit.");
                command(Redo);
                auto doubleClickText = [&] {
                    const auto point = app.view.toScreen(app.document.items.back().a + Point{15, 15});
                    SendMessageW(window, WM_LBUTTONDBLCLK, MK_LBUTTON,
                        MAKELPARAM(static_cast<int>(point.x * app.dpi), static_cast<int>(point.y * app.dpi)));
                    if (!app.textEdit)
                        throw std::runtime_error("Double-click did not reopen text editing.");
                };
                doubleClickText();
                SetWindowTextW(app.textEdit, L"Canceled changes");
                SendMessageW(app.textEdit, WM_KEYDOWN, VK_ESCAPE, 0);
                if (app.textEdit || app.document.items.back().text != L"Review this value")
                    throw std::runtime_error("Canceling text editing failed to restore the original.");
                doubleClickText();
                SetWindowTextW(app.textEdit, L"Review this value\r\nBefore sharing");
                updateTextFromEditor();
                RECT multilineField{};
                GetClientRect(app.textEdit, &multilineField);
                if (SendMessageW(app.textEdit, EM_GETLINECOUNT, 0, 0) != 2 ||
                    multilineField.bottom <= shortField.bottom)
                    throw std::runtime_error("Inline editor did not grow vertically for a new line.");
                verifyInline(L"smoke-test-inline-multiline.png");
                finishTextEditing();
                command(Undo);
                if (app.document.items.back().text != L"Review this value")
                    throw std::runtime_error("Editing existing text was not undoable.");
                command(Redo);
                auto textBounds = app.document.items.back().bounds();
                dragImage({textBounds.left + 15, textBounds.top + 15},
                          {textBounds.left + 25, textBounds.top + 5});
                if (length(app.document.items.back().a - Point{260, 260}) > 2)
                    throw std::runtime_error("Text annotation movement failed.");
                textBounds = app.document.items.back().bounds();
                const float oldFontSize = app.document.items.back().fontSize;
                dragImage({textBounds.right, textBounds.bottom},
                          {textBounds.right + 30, textBounds.bottom + 20});
                if (app.document.items.back().fontSize <= oldFontSize)
                    throw std::runtime_error("Text corner resizing did not scale the font.");
                command(Undo);
                clickButton(TextTool);
                dragImage({20, 200}, {20, 200});
                SendMessageW(app.textEdit, WM_KEYDOWN, VK_ESCAPE, 0);
                if (app.document.items.size() != beforeText + 1 || app.document.editing())
                    throw std::runtime_error("Canceling empty text left an annotation or pending edit.");
                for (int style = 0; style < 4; ++style)
                {
                    command(styleCommand(Tool::Rectangle, style));
                    dragImage({20, 220}, {180, 260});
                    if (app.document.items.back().kind != Tool::Rectangle ||
                        app.document.items.back().style != style || app.tool != Tool::Select)
                        throw std::runtime_error("Rectangle placement or style selection failed.");
                }
                command(CircleTool);
                if (app.tool != Tool::Rectangle)
                    throw std::runtime_error("Combined shape button did not remember its rectangle choice.");
                for (int id : {CircleStyleMenu, ArrowStyleMenu, CheckStyleMenu, LineStyleMenu})
                {
                    smokeMenuPreviewPath = L"smoke-test-menu-" + std::to_wstring(id) + L".png";
                    smokeMenuPreviewError.clear();
                    if (!SetTimer(window, 99, 100, smokeShapeMenuTimer))
                        throw std::runtime_error("Cannot schedule visual shape menu test.");
                    command(id);
                    if (!smokeMenuPreviewError.empty())
                        throw std::runtime_error(smokeMenuPreviewError);
                }
                auto flattened = app.graphics.flatten(app.image, app.document.items);
                saveBytes(L"smoke-test-export.png", app.graphics.png(flattened));
                for (bool border : {true, false})
                {
                    SendMessageW(window, WM_COMMAND, ProfessionalBorder, 0);
                    updateMenus();
                    if (app.exportOptions.professionalBorder != border || !app.exportPreferencesDirty ||
                        bool(GetMenuState(GetMenu(window), ProfessionalBorder, MF_BYCOMMAND) & MF_CHECKED) != border)
                        throw std::runtime_error("Professional Border Settings toggle or checkmark failed.");
                    const auto exported = renderedExport();
                    if (exported.width != app.image.width + (border ? 40 : 0) ||
                        exported.height != app.image.height + (border ? 40 : 0) ||
                        (!border && exported.pixels != flattened.pixels))
                        throw std::runtime_error("Shared export pipeline did not honor Professional Border.");
                    app.savePath = border ? L"smoke-test-professional-border.png" : L"smoke-test-border-off.png";
                    command(Save);
                    std::ifstream saved(std::filesystem::path(app.savePath), std::ios::binary);
                    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(saved)), {});
                    const auto decoded = app.graphics.decode(bytes);
                    if (decoded.width != exported.width || decoded.height != exported.height ||
                        decoded.pixels != exported.pixels || app.dirty)
                        throw std::runtime_error("Save PNG did not bake in the selected border effect.");
                }
                command(ProfessionalBorder); // Leave ON to verify saved preferences across process
                                             // exit.
                if (GetMenuItemCount(app.professionalMenu) != 4 ||
                    !app.exportOptions.professionalBlur || !app.exportOptions.professionalRounded)
                    throw std::runtime_error(
                        "Enabling Professional Border must enable blur and rounded corners.");
                for (bool blur : {false, true})
                    for (bool rounded : {false, true})
                    {
                        if (app.exportOptions.professionalBlur != blur)
                            command(ProfessionalBlur);
                        if (app.exportOptions.professionalRounded != rounded)
                            command(ProfessionalRounded);
                        updateMenus();
                        if (!app.exportOptions.professionalBorder ||
                            bool(
                                GetMenuState(app.professionalMenu, ProfessionalBlur, MF_BYCOMMAND) &
                                MF_CHECKED) != blur ||
                            bool(GetMenuState(app.professionalMenu, ProfessionalRounded,
                                              MF_BYCOMMAND) &
                                 MF_CHECKED) != rounded)
                            throw std::runtime_error(
                                "Professional Border component menu checkmarks failed.");
                        const auto exported = renderedExport();
                        if (exported.width != app.image.width + (blur ? 40 : 0) ||
                            exported.height != app.image.height + (blur ? 40 : 0) ||
                            (!blur && !rounded && exported.pixels != flattened.pixels))
                            throw std::runtime_error("Independent professional effects did not "
                                                     "reach the shared renderer.");
                        app.savePath = L"smoke-test-professional-" + std::to_wstring(blur) + L"-" +
                                       std::to_wstring(rounded) + L".png";
                        command(Save);
                        std::ifstream saved(std::filesystem::path(app.savePath), std::ios::binary);
                        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(saved)),
                                                         {});
                        if (app.graphics.decode(bytes).pixels != exported.pixels)
                            throw std::runtime_error("Save did not bake in an independent "
                                                     "professional effect combination.");
                    }
                command(ProfessionalRounded);
                command(ProfessionalBorder);
                updateMenus();
                if (!(GetMenuState(app.professionalMenu, ProfessionalBlur, MF_BYCOMMAND) &
                      MF_GRAYED) ||
                    !(GetMenuState(app.professionalMenu, ProfessionalRounded, MF_BYCOMMAND) &
                      MF_GRAYED))
                    throw std::runtime_error(
                        "Disabled professional components should be unavailable in the submenu.");
                command(ProfessionalBorder);
                if (!app.exportOptions.professionalBlur || !app.exportOptions.professionalRounded)
                    throw std::runtime_error(
                        "Re-enabling Professional Border did not restore both components.");
                smokeMenuPreviewPath = L"smoke-test-professional-menu.png";
                smokeMenuPreviewError.clear();
                updateMenus();
                app.shapeMenu = app.professionalMenu;
                if (!SetTimer(window, 99, 100, smokeShapeMenuTimer))
                    throw std::runtime_error("Cannot inspect the Professional Border submenu.");
                POINT professionalAnchor{20, 40};
                ClientToScreen(window, &professionalAnchor);
                SetForegroundWindow(window);
                TrackPopupMenu(app.professionalMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                               professionalAnchor.x, professionalAnchor.y, 0, window, nullptr);
                app.shapeMenu = nullptr;
                if (!smokeMenuPreviewError.empty())
                    throw std::runtime_error(smokeMenuPreviewError);
                for (bool logo : {true, false})
                {
                    SendMessageW(window, WM_COMMAND, SamtecLogo, 0);
                    updateMenus();
                    if (app.exportOptions.samtecLogo != logo ||
                        bool(GetMenuState(GetMenu(window), SamtecLogo, MF_BYCOMMAND) & MF_CHECKED) != logo)
                        throw std::runtime_error("Samtec Logo Settings toggle or checkmark failed.");
                    const auto exported = renderedExport();
                    if (exported.width != app.image.width + 40 || exported.height != app.image.height + 40)
                        throw std::runtime_error("Samtec Logo changed exported screenshot dimensions.");
                    app.savePath = logo ? L"smoke-test-samtec-logo.png" : L"smoke-test-samtec-off.png";
                    command(Save);
                    std::ifstream saved(std::filesystem::path(app.savePath), std::ios::binary);
                    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(saved)), {});
                    if (app.graphics.decode(bytes).pixels != exported.pixels)
                        throw std::runtime_error("PNG Save did not honor the Samtec Logo setting.");
                }
                command(SamtecLogo); // Verify the logo setting survives close and full exit.
                if (GetMenuItemCount(app.logoMenu) != 8)
                    throw std::runtime_error("Samtec Logo submenu must contain Enabled and six styles.");
                for (int style = 0; style < 6; ++style)
                {
                    command(LogoStyleFirst + style);
                    updateMenus();
                    if (!app.exportOptions.samtecLogo || app.exportOptions.samtecStyle != style ||
                        !(GetMenuState(app.logoMenu, LogoStyleFirst + style, MF_BYCOMMAND) & MF_CHECKED))
                        throw std::runtime_error("Samtec Logo style selection or radio indicator failed.");
                    const auto exported = renderedExport();
                    app.savePath = L"smoke-test-samtec-style-" + std::to_wstring(style + 1) + L".png";
                    command(Save);
                    std::ifstream saved(std::filesystem::path(app.savePath), std::ios::binary);
                    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(saved)), {});
                    if (app.graphics.decode(bytes).pixels != exported.pixels)
                        throw std::runtime_error("Save PNG did not honor a Samtec logo style.");
                }
                smokeMenuPreviewPath = L"smoke-test-samtec-menu.png";
                smokeMenuPreviewError.clear();
                app.shapeMenu = app.logoMenu;
                if (!SetTimer(window, 99, 100, smokeShapeMenuTimer))
                    throw std::runtime_error("Cannot inspect the Samtec style menu.");
                POINT menuAnchor{20, 40};
                ClientToScreen(window, &menuAnchor);
                SetForegroundWindow(window);
                TrackPopupMenu(app.logoMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                    menuAnchor.x, menuAnchor.y, 0, window, nullptr);
                app.shapeMenu = nullptr;
                if (!smokeMenuPreviewError.empty())
                    throw std::runtime_error(smokeMenuPreviewError);
                const auto previewTestOptions = app.exportOptions;
                const auto previewTestView = app.view;
                const bool previewTestFit = app.fit;
                const int previewTestSelection = app.document.selected;
                app.document.selected = -1;
                auto verifyStyledPreview = [&]() {
                    app.fit = false;
                    app.view.scale = 1 / app.dpi;
                    centerView();
                    const float inset = previewPadding() * app.view.scale;
                    app.view.origin.x =
                        std::round((app.view.origin.x - inset) * app.dpi) / app.dpi + inset;
                    app.view.origin.y =
                        std::round((app.view.origin.y - inset) * app.dpi) / app.dpi + inset;
                    updateView();
                    const auto exported = renderedExport();
                    const auto &styled = previewImage();
                    if (styled.width != exported.width || styled.height != exported.height ||
                        styled.pixels != exported.pixels)
                        throw std::runtime_error(
                            "The styled preview differs from copied/saved image pixels.");
                    const auto *cachedPixels = styled.pixels.data();
                    if (previewImage().pixels.data() != cachedPixels)
                        throw std::runtime_error(
                            "Unchanged preview unnecessarily regenerated its image.");
                    const auto scene = renderEditorPreview();
                    const auto canvas = canvasRect();
                    const int dx =
                        static_cast<int>(std::lround((app.view.origin.x - inset) * app.dpi));
                    const int dy =
                        static_cast<int>(std::lround((app.view.origin.y - inset) * app.dpi));
                    size_t checked = 0;
                    for (int y = 0; y < styled.height; y += 13)
                        for (int x = 0; x < styled.width; x += 13)
                        {
                            const int sx = dx + x, sy = dy + y;
                            const Point point{sx / app.dpi, sy / app.dpi};
                            if (!canvas.contains(point, -3))
                                continue;
                            const double gx = std::remainder(point.x + .5 / app.dpi - 18, 24);
                            const double gy =
                                std::remainder(point.y + .5 / app.dpi - canvas.top - 18, 24);
                            if (std::abs(gx) < 3 && std::abs(gy) < 3)
                                continue; // Compare over the uniform workspace away from its dots.
                            const size_t src = (static_cast<size_t>(y) * styled.width + x) * 4;
                            const size_t dst = (static_cast<size_t>(sy) * scene.width + sx) * 4;
                            const int background[] = {251, 247, 246};
                            for (int channel = 0; channel < 3; ++channel)
                            {
                                const int alpha = styled.pixels[src + 3];
                                const int expected = (styled.pixels[src + channel] * alpha +
                                                      background[channel] * (255 - alpha) + 127) /
                                                     255;
                                if (std::abs(int(scene.pixels[dst + channel]) - expected) > 2)
                                    throw std::runtime_error(
                                        "The editor did not draw the exported logo, halo, or "
                                        "rounded alpha correctly.");
                            }
                            ++checked;
                        }
                    if (checked < 100)
                        throw std::runtime_error(
                            "Styled preview QA did not inspect enough visible pixels.");
                    return scene;
                };
                for (bool professional : {false, true})
                    for (bool blur : {false, true})
                        for (bool rounded : {false, true})
                            for (bool logo : {false, true})
                            {
                                app.exportOptions.professionalBorder = professional;
                                app.exportOptions.professionalBlur = blur;
                                app.exportOptions.professionalRounded = rounded;
                                app.exportOptions.samtecLogo = logo;
                                verifyStyledPreview();
                            }
                app.exportOptions = previewTestOptions;
                for (uint8_t style = 0; style < 6; ++style)
                {
                    command(LogoStyleFirst + style);
                    const auto scene = verifyStyledPreview();
                    saveBytes(L"smoke-test-styled-preview-" + std::to_wstring(style + 1) + L".png",
                              app.graphics.png(scene));
                }
                const auto originalPreviewColor = app.document.items.front().color;
                app.document.items.front().color = rgb(12, 34, 56);
                verifyStyledPreview();
                app.document.items.front().color = originalPreviewColor;
                app.exportOptions = previewTestOptions;
                app.view = previewTestView;
                app.fit = previewTestFit;
                app.document.selected = previewTestSelection;
                resetPreview();
                const auto beforeFeedback = renderEditorPreview();
                const auto beforeFeedbackExport = renderedExport();
                startCopyFeedback(); // Exercise feedback without touching the user's clipboard.
                const auto feedback = renderEditorPreview();
                if (!app.copyFlashStarted || feedback.pixels == beforeFeedback.pixels ||
                    renderedExport().pixels != beforeFeedbackExport.pixels)
                    throw std::runtime_error("Copy flash was invisible or changed exported pixels.");
                saveBytes(L"smoke-test-copy-flash.png", app.graphics.png(feedback));
                app.copyFlashStarted = GetTickCount64() - 400;
                SendMessageW(window, WM_TIMER, CopyFlashTimer, 0);
                if (app.copyFlashStarted || renderEditorPreview().pixels != beforeFeedback.pixels)
                    throw std::runtime_error("Copy flash did not expire cleanly.");
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
                            (b.command != CenterView && b.rect.bottom > toolbarHeight()))
                            throw std::runtime_error("Compact toolbar control is clipped.");
                    saveBytes(L"smoke-test-layout-" +
                                  std::to_wstring(static_cast<int>(scale * 100)) + L".png",
                              app.graphics.png(renderEditorPreview()));
                    command(TextTool);
                    buildButtons();
                    for (const auto &b : app.buttons)
                        if (b.rect.right > bounds.right || (b.command != CenterView && b.rect.bottom > toolbarHeight()))
                            throw std::runtime_error("Text formatting control is clipped at high DPI.");
                    saveBytes(L"smoke-test-text-layout-" +
                                  std::to_wstring(static_cast<int>(scale * 100)) + L".png",
                              app.graphics.png(renderEditorPreview()));
                    command(SelectTool);
                }
                app.dpi = normalDpi;
                if (app.target)
                    app.target->SetDpi(normalDpi * 96, normalDpi * 96);
                SetWindowPos(window, nullptr, normalBounds.left, normalBounds.top,
                             normalBounds.right - normalBounds.left,
                             normalBounds.bottom - normalBounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
                // Navigation changes the view only, never the screenshot or export.
                const auto navigationImage = app.image;
                app.image = Bitmap::create(2000, 1500);
                std::fill(app.image.pixels.begin(), app.image.pixels.end(), 255);
                resetPreview();
                command(Actual);
                buildButtons();
                if (std::abs(app.view.scale * app.dpi - 1) > .001f || !app.horizontalVisible ||
                    !app.verticalVisible || !IsWindowVisible(app.horizontalScroll) ||
                    !IsWindowVisible(app.verticalScroll))
                    throw std::runtime_error("Actual size did not show usable native scrollbars.");
                const auto initialOrigin = app.view.origin;
                SendMessageW(window, WM_HSCROLL, SB_LINEDOWN,
                             reinterpret_cast<LPARAM>(app.horizontalScroll));
                SendMessageW(window, WM_VSCROLL, SB_PAGEDOWN,
                             reinterpret_cast<LPARAM>(app.verticalScroll));
                if (app.view.origin.x >= initialOrigin.x || app.view.origin.y >= initialOrigin.y)
                    throw std::runtime_error(
                        "Scrollbar arrows or page scrolling did not pan the image.");
                command(CenterView);
                if (length(app.view.origin - initialOrigin) > .02f ||
                    std::abs(app.view.scale * app.dpi - 1) > .001f)
                    throw std::runtime_error(
                        "Center changed zoom or failed to recenter a large screenshot.");
                const auto scrollbarPreview = renderEditorPreview();
                saveBytes(L"smoke-test-scrollbars.png", app.graphics.png(scrollbarPreview));
                for (HWND control : {app.horizontalScroll, app.verticalScroll})
                {
                    SCROLLBARINFO geometry{};
                    geometry.cbSize = sizeof(geometry);
                    if (!GetScrollBarInfo(control, OBJID_CLIENT, &geometry) ||
                        geometry.xyThumbBottom <= geometry.xyThumbTop ||
                        geometry.rcScrollBar.right <= geometry.rcScrollBar.left ||
                        geometry.rcScrollBar.bottom <= geometry.rcScrollBar.top)
                        throw std::runtime_error("Native scrollbar geometry is unavailable.");
                    RECT bounds{};
                    GetClientRect(control, &bounds);
                    POINT origin{};
                    MapWindowPoints(control, window, &origin, 1);
                    const float thumb = (geometry.xyThumbTop + geometry.xyThumbBottom) / 2.0f;
                    const Point pixel =
                        control == app.horizontalScroll
                            ? Point{origin.x + thumb, origin.y + bounds.bottom / 2.0f}
                            : Point{origin.x + bounds.right / 2.0f, origin.y + thumb};
                    if (scrollbarPreview.sample(pixel) != rgb(173, 179, 199))
                        throw std::runtime_error("Scrollbar handles are not visibly rendered.");
                }
                auto wheelScroll = [&](bool horizontal, int delta) {
                    SendMessageW(window, WM_MOUSEWHEEL,
                        MAKEWPARAM(horizontal ? MK_SHIFT : 0, static_cast<WORD>(delta)), 0);
                };
                // Oversized images stop at their actual edges, with persistent scrollbars.
                for (bool horizontal : {false, true})
                {
                    const auto message = horizontal ? WM_HSCROLL : WM_VSCROLL;
                    const HWND control = horizontal ? app.horizontalScroll : app.verticalScroll;
                    for (int end : {SB_TOP, SB_BOTTOM})
                    {
                        SendMessageW(window, message, end, reinterpret_cast<LPARAM>(control));
                        const auto bounds = canvasRect();
                        const float expected =
                            horizontal
                                ? (end == SB_TOP
                                       ? bounds.left + previewPadding() * app.view.scale
                                       : bounds.right -
                                             (app.image.width + previewPadding()) * app.view.scale)
                                : (end == SB_TOP
                                       ? bounds.top + previewPadding() * app.view.scale
                                       : bounds.bottom - (app.image.height + previewPadding()) *
                                                             app.view.scale);
                        if (std::abs((horizontal ? app.view.origin.x : app.view.origin.y) -
                                     expected) > .02f)
                            throw std::runtime_error(
                                "Scrolling did not stop at the screenshot edge.");
                        const auto edge = app.view.origin;
                        for (int i = 0; i < 5; ++i)
                            wheelScroll(horizontal, end == SB_TOP ? WHEEL_DELTA : -WHEEL_DELTA);
                        if (length(app.view.origin - edge) > .02f || !app.horizontalVisible ||
                            !app.verticalVisible || !IsWindowVisible(app.horizontalScroll) ||
                            !IsWindowVisible(app.verticalScroll))
                            throw std::runtime_error(
                                "Wheel overscrolled an edge or hid required scrollbars.");
                    }
                }
                const auto largeImage = app.image;
                const auto available = workspaceRect();
                struct ScrollCase
                {
                    float width, height;
                    bool horizontal, vertical;
                };
                const ScrollCase scrollCases[] = {
                    {available.width() / 2, available.height() / 2, false, false},
                    {available.width(), available.height(), false, false},
                    {available.width() + 80, available.height() / 2, true, false},
                    {available.width() / 2, available.height() + 80, false, true},
                    {available.width() - 8, available.height() + 80, true, true},
                    {available.width() + 80, available.height() - 8, true, true}};
                for (const auto &sample : scrollCases)
                {
                    app.image = Bitmap::create(
                        static_cast<int>(sample.width * app.dpi) - previewPadding() * 2,
                        static_cast<int>(sample.height * app.dpi) - previewPadding() * 2);
                    resetPreview();
                    command(Actual);
                    if (app.horizontalVisible != sample.horizontal ||
                        app.verticalVisible != sample.vertical ||
                        bool(IsWindowVisible(app.horizontalScroll)) != sample.horizontal ||
                        bool(IsWindowVisible(app.verticalScroll)) != sample.vertical)
                        throw std::runtime_error(
                            "Scrollbars did not match image/viewport dimensions.");
                    for (bool horizontal : {false, true})
                    {
                        const bool scrollable = horizontal ? sample.horizontal : sample.vertical;
                        const auto before = app.view.origin;
                        wheelScroll(horizontal, -WHEEL_DELTA);
                        const float change = horizontal ? app.view.origin.x - before.x
                                                        : app.view.origin.y - before.y;
                        const float otherChange = horizontal ? app.view.origin.y - before.y
                                                             : app.view.origin.x - before.x;
                        if (scrollable ? change >= 0 : change != 0)
                            throw std::runtime_error(
                                "Wheel scrolling ignored axis overflow rules.");
                        if (otherChange != 0)
                            throw std::runtime_error("Wheel scrolling moved the other axis.");
                        if (!scrollable)
                        {
                            SendMessageW(window, horizontal ? WM_HSCROLL : WM_VSCROLL, SB_BOTTOM,
                                         reinterpret_cast<LPARAM>(horizontal ? app.horizontalScroll
                                                                             : app.verticalScroll));
                            if (length(app.view.origin - before) > .001f)
                                throw std::runtime_error(
                                    "A hidden scrollbar moved a fitting image.");
                        }
                    }
                    if (!sample.horizontal && !sample.vertical)
                    {
                        const auto before = app.view.origin;
                        const auto canvas = canvasRect();
                        const auto point =
                            MAKELPARAM(static_cast<int>((canvas.left + 30) * app.dpi),
                                       static_cast<int>((canvas.top + 30) * app.dpi));
                        SendMessageW(window, WM_MBUTTONDOWN, MK_MBUTTON, point);
                        SendMessageW(window, WM_MOUSEMOVE, MK_MBUTTON, MAKELPARAM(0, 0));
                        SendMessageW(window, WM_MBUTTONUP, 0, point);
                        if (app.drag != Drag::None || GetCapture() == window ||
                            length(app.view.origin - before) > .001f)
                            throw std::runtime_error(
                                "A fitting image could still be dragged to scroll.");
                        app.view.origin = {canvas.left - 100, canvas.bottom + 100};
                        updateView();
                        if (app.horizontalVisible || app.verticalVisible ||
                            app.view.origin.x < canvas.left ||
                            app.view.origin.y + app.image.height * app.view.scale >
                                canvas.bottom + .01f)
                            throw std::runtime_error("A stale offset created unnecessary "
                                                     "scrollbars or hid image pixels.");
                    }
                    command(Fit);
                    const auto fittedOrigin = app.view.origin;
                    wheelScroll(false, -WHEEL_DELTA);
                    wheelScroll(true, WHEEL_DELTA);
                    if (!app.fit || length(app.view.origin - fittedOrigin) > .001f ||
                        app.horizontalVisible || app.verticalVisible)
                        throw std::runtime_error("Wheel scrolling moved an image in Fit mode.");
                }
                app.image = largeImage;
                resetPreview();
                command(Fit);
                const auto beforeWheelZoom = app.view.scale;
                const auto zoomCanvas = canvasRect();
                POINT zoomAnchor{
                    static_cast<LONG>((zoomCanvas.left + zoomCanvas.right) * app.dpi / 2),
                    static_cast<LONG>((zoomCanvas.top + zoomCanvas.bottom) * app.dpi / 2)};
                ClientToScreen(window, &zoomAnchor);
                SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA),
                             MAKELPARAM(zoomAnchor.x, zoomAnchor.y));
                updateView();
                if (app.fit || std::abs(app.view.scale - beforeWheelZoom * 1.2f) > .0001f)
                    throw std::runtime_error(
                        "Ctrl+wheel stopped zooming when ordinary scrolling was disabled.");
                command(Fit);
                if (app.horizontalVisible || app.verticalVisible ||
                    IsWindowVisible(app.horizontalScroll) || IsWindowVisible(app.verticalScroll))
                    throw std::runtime_error("Fit left unnecessary scrollbars visible.");
                const auto fitScale = app.view.scale;
                const auto r = canvasRect();
                zoomAt({r.left + 20, r.top + 20}, .7f);
                updateView();
                const auto floatingCenter = centerButtonRect();
                if (!floatingCenter)
                    throw std::runtime_error(
                        "Zooming out did not offer Center in the empty workspace.");
                const auto imageLeft = app.view.origin.x, imageTop = app.view.origin.y;
                const auto imageRight = imageLeft + app.image.width * app.view.scale;
                const auto imageBottom = imageTop + app.image.height * app.view.scale;
                if (!(floatingCenter->right < imageLeft || floatingCenter->left > imageRight ||
                      floatingCenter->bottom < imageTop || floatingCenter->top > imageBottom))
                    throw std::runtime_error("Floating Center covers screenshot pixels.");
                saveBytes(L"smoke-test-center.png", app.graphics.png(renderEditorPreview()));
                const auto zoomedScale = app.view.scale;
                const auto floatingClick = MAKELPARAM(
                    static_cast<int>((floatingCenter->left + floatingCenter->right) * app.dpi / 2),
                    static_cast<int>((floatingCenter->top + floatingCenter->bottom) * app.dpi / 2));
                SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, floatingClick);
                SendMessageW(window, WM_LBUTTONUP, 0, floatingClick);
                if (centerButtonRect() || std::abs(app.view.scale - zoomedScale) > .0001f)
                    throw std::runtime_error(
                        "Center did not preserve zoom or hide itself after centering.");
                command(Fit);
                if (std::abs(app.view.scale - fitScale) > .0001f)
                    throw std::runtime_error("Fit changed after panning and centering.");
                // Every combination retains accessible expand controls, at each DPI.
                for (float scale : {1.0f, 1.5f, 2.0f})
                {
                    app.dpi = scale;
                    SetWindowPos(window, nullptr, 0, 0, static_cast<int>(850 * scale),
                                 static_cast<int>(430 * scale),
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                    app.dpi = scale;
                    for (unsigned mask = 0; mask < 8; ++mask)
                    {
                        app.collapsedRows = mask;
                        updateView();
                        buildButtons();
                        for (const auto &b : app.buttons)
                            if (b.command != CenterView &&
                                (b.rect.left < 0 || b.rect.right > clientDips().right ||
                                 b.rect.top < 0 || b.rect.bottom > toolbarHeight()))
                                throw std::runtime_error("Collapsed toolbar controls are clipped.");
                        for (int row = 0; row < 3; ++row)
                            if (std::none_of(app.buttons.begin(), app.buttons.end(),
                                             [&](const Button &b) {
                                                 return b.command == ToggleActions + row;
                                             }))
                                throw std::runtime_error("Collapsed row lost its expand button.");
                    }
                }
                app.dpi = normalDpi;
                app.collapsedRows = 0;
                SetWindowPos(window, nullptr, normalBounds.left, normalBounds.top,
                             normalBounds.right - normalBounds.left,
                             normalBounds.bottom - normalBounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
                if (app.target)
                    app.target->SetDpi(app.dpi * 96, app.dpi * 96);
                updateView();
                buildButtons();
                clickButton(ToggleActions);
                if (app.collapsedRows != 1)
                    throw std::runtime_error("Actions collapse button failed.");
                clickButton(ToggleActions);
                clickButton(ToggleTools);
                clickButton(ToggleFormatting);
                if (app.collapsedRows != 6 || toolbarHeight() != 100)
                    throw std::runtime_error("Toolbar collapse buttons failed.");
                saveBytes(L"smoke-test-collapsed.png", app.graphics.png(renderEditorPreview()));
                const auto windowStyle = GetWindowLongPtrW(window, GWL_STYLE);
                RECT beforeFullScreen{};
                GetWindowRect(window, &beforeFullScreen);
                const auto menuBeforeFullScreen = GetMenu(window);
                SendMessageW(window, WM_KEYDOWN, VK_F11, 0);
                if (!app.fullScreen || GetMenu(window) || toolbarHeight() != 0 ||
                    canvasRect().top != 0 || app.collapsedRows != 6)
                    throw std::runtime_error(
                        "Full screen failed to hide chrome or preserve toolbar preferences.");
                saveBytes(L"smoke-test-full-screen.png", app.graphics.png(renderEditorPreview()));
                SendMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
                RECT afterFullScreen{};
                GetWindowRect(window, &afterFullScreen);
                if (app.fullScreen || GetMenu(window) != menuBeforeFullScreen ||
                    app.collapsedRows != 6 || GetWindowLongPtrW(window, GWL_STYLE) != windowStyle ||
                    beforeFullScreen.left != afterFullScreen.left ||
                    beforeFullScreen.top != afterFullScreen.top ||
                    beforeFullScreen.right != afterFullScreen.right ||
                    beforeFullScreen.bottom != afterFullScreen.bottom)
                    throw std::runtime_error("Esc did not restore the window after full screen.");
                ShowWindow(window, SW_MAXIMIZE);
                command(FullScreen);
                MONITORINFO fullScreenMonitor{};
                fullScreenMonitor.cbSize = sizeof(fullScreenMonitor);
                GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
                                &fullScreenMonitor);
                RECT fullScreenBounds{};
                GetWindowRect(window, &fullScreenBounds);
                if (fullScreenBounds.left != fullScreenMonitor.rcMonitor.left ||
                    fullScreenBounds.top != fullScreenMonitor.rcMonitor.top ||
                    fullScreenBounds.right != fullScreenMonitor.rcMonitor.right ||
                    fullScreenBounds.bottom != fullScreenMonitor.rcMonitor.bottom)
                    throw std::runtime_error(
                        "Full screen from a maximized window did not fill the monitor.");
                command(FullScreen);
                if (!IsZoomed(window))
                    throw std::runtime_error("Full screen failed to restore a maximized window.");
                ShowWindow(window, SW_RESTORE);
                app.collapsedRows = 0;
                app.image = navigationImage;
                resetPreview();
                command(Fit);
                buildButtons();
                wchar_t saveTestDirectory[32768]{};
                GetCurrentDirectoryW(32768, saveTestDirectory);
                const auto saveTestFolder =
                    (std::filesystem::path(saveTestDirectory) / L"smoke-save location").wstring();
                std::filesystem::create_directories(saveTestFolder);
                const auto previousSavePath = app.savePath;
                setSaveFolder(saveTestFolder);
                if (app.savePath != previousSavePath ||
                    GetMenuState(GetMenu(window), SaveLocation, MF_BYCOMMAND) == UINT(-1) ||
                    initialSavePath(L"C:\\old-folder\\example.png") !=
                        (std::filesystem::path(saveTestFolder) / L"example.png").wstring())
                    throw std::runtime_error(
                        "Save location menu, initial folder, or existing save path failed.");
                app.saveFolder.clear();
                loadToolPreferences();
                if (app.saveFolder != saveTestFolder)
                    throw std::runtime_error("Save location was not remembered immediately.");
                app.saveFolder =
                    (std::filesystem::path(saveTestFolder) / L"missing folder").wstring();
                if (initialSavePath(previousSavePath) != previousSavePath)
                    throw std::runtime_error(
                        "Unavailable save location did not fall back to the original path.");
                app.saveFolder = saveTestFolder;
                bool rejectedMissingFolder = false;
                try
                {
                    setSaveFolder(
                        (std::filesystem::path(saveTestFolder) / L"missing folder").wstring());
                }
                catch (const std::runtime_error &)
                {
                    rejectedMissingFolder = true;
                }
                if (!rejectedMissingFolder || app.saveFolder != saveTestFolder)
                    throw std::runtime_error("Invalid save location replaced the current setting.");
                app.savePath = initialSavePath(L"saved-snip.png");
                saveImage();
                std::ifstream folderSaved(std::filesystem::path(app.savePath), std::ios::binary);
                const std::vector<uint8_t> folderSavedBytes(
                    (std::istreambuf_iterator<char>(folderSaved)), {});
                if (app.graphics.decode(folderSavedBytes).pixels != renderedExport().pixels)
                    throw std::runtime_error(
                        "Save to the configured folder changed exported pixels.");
                app.savePath = previousSavePath;
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
                for (int i = 1; i < static_cast<int>(app.colors.size()); ++i)
                {
                    const Tool tool = static_cast<Tool>(i);
                    if (StyleCounts[i] > 1)
                        command(
                            styleCommand(tool, tool == Tool::Check ? 4 : PersistenceTestStyles[i]));
                    else
                        selectTool(tool);
                    changeColor(PersistenceTestColors[i]);
                }
                app.collapsedRows =
                    0; // Establish the persistence fixture independently of prior test runs.
                command(ToggleTools);
                command(ToggleFormatting);
                command(LogoStyleFirst + 5);
                const auto closeColors = app.colors;
                const auto closeStyles = app.styles;
                if (app.exportOptions.professionalBlur)
                    command(ProfessionalBlur); // Persist a custom rounded-only combination on every run.
                changeTextFormatting(40, true, true);
                beginTextEditing({20, 20});
                SendMessageW(app.textEdit, WM_CHAR, 'X', 0);
                app.dirty = true;
                SendMessageW(window, WM_CLOSE, 0, 0);
                if (SmokeNoPromptGuard::shown || hasImage() || IsWindowVisible(window) ||
                    app.toolPreferencesDirty || app.exportPreferencesDirty ||
                    app.layoutPreferencesDirty || app.textEdit || app.textEditBackground ||
                    app.document.editing())
                    throw std::runtime_error("Closing to the tray did not save tool preferences.");
                app.colors.fill(Palette[0]);
                app.styles.fill(0);
                app.exportOptions.professionalBorder = false;
                app.exportOptions.professionalBlur = true;
                app.exportOptions.professionalRounded = false;
                app.exportOptions.samtecLogo = false;
                app.exportOptions.samtecStyle = 0;
                const auto closedTool = app.tool;
                loadToolPreferences();
                if (app.colors != closeColors || app.styles != closeStyles || app.tool != closedTool)
                    throw std::runtime_error("Closing lost tool colors/styles or changed the active tool.");
                if (app.fontSize != 40 || !app.textBold || !app.textBox || app.geometryTool != Tool::Rectangle ||
                    !app.exportOptions.professionalBorder || !app.exportOptions.samtecLogo || app.collapsedRows != 6 ||
                    app.exportOptions.professionalBlur || !app.exportOptions.professionalRounded ||
                    app.exportOptions.samtecStyle != 5)
                    throw std::runtime_error("Text formatting or geometry group preferences were lost.");
                // A later change must be written by the full-exit path, not the earlier close.
                app.image = Bitmap::create(10, 10);
                command(styleCommand(Tool::Check, 5));
                if (activeColor() != PersistenceTestColors[static_cast<size_t>(Tool::Check)])
                    throw std::runtime_error("Reopening a tool lost its previous custom color.");
                app.document.items.push_back(unsaved);
                app.dirty = true; // The final File > Exit must also proceed without a prompt.
                writeTestReport(
                    L"smoke-test-results.txt",
                    "PASS: unsaved new snips, closing, and File > Exit proceed without save confirmation, "
                    "closing discards active text editing, repeated capture shortcuts ignored, "
                    "no editor/fade pixels in capture, restored editor with Pen selected, "
                    "instant hotkey freezes focus-dismissed hover popup pixels before selection, "
                    "visible and hidden editor, instant cancellation preserves the previous snip, "
                    "native window, Direct2D editor, live desktop capture, selection overlay "
                    "original pixels, mouse rectangle selection and cropping, mouse drawing, all "
                    "stickers, red X click/drag/move/resize/delete/undo/redo, check/X default and custom colors, "
                    "solid/dashed/dotted lines and endpoint editing, outlined/curved "
                    "arrows, straight and block gloss arrows, "
                    "move/resize/recolor, arrow endpoint rotation, delete, held +/- font/brush repeat, bounds, pause/resume, cancellation, single-step undo, toolbar press/release "
                    "and cancellation, mouse undo/redo, eyedropper toolbar/shortcut/cancel, "
                    "sampling image and annotation colors at multiple zoom levels, undoable picker recolor, "
                    "pen cursor color and size, click-to-type text, click-away commits and returns to Select, "
                    "matching native/export annotation typefaces, auto-sized borderless inline editing, "
                    "native plain/boxed/multiline typing backgrounds, growth/deletion/line wrapping, "
                    "live font/bold/color/box formatting, "
                    "Ctrl+Enter, double-click editing, Escape rollback, text undo/redo/move/resize, "
                    "rectangle styles, native visual shape dropdowns, compact text layouts at 100/150/200% DPI, "
                    "annotated PNG export, Professional Border Settings toggle/checkmark, "
                    "actual PNG save with every independent blur/rounding combination, professional submenu and persistence, Samtec Logo toggle/checkmark, six visual styles and PNG Save, "
                    "styled preview/export equality for all settings and logo styles, alpha compositing, cache invalidation, copy flash visibility, expiry, and unchanged exports, "
                    "save location menu, folder preference, existing-file preservation, unavailable-folder fallback, "
                    "PNG save in configured folder, native scrollbar panning, wheel/pan overflow gating, edge bounds, stable scrollbar visibility, zoom-preserving Center outside image, "
                    "all collapsed row combinations at 100/150/200% DPI, full screen F11/Esc and window restoration, "
                    "settings dialog and persistence, per-tool styles/custom colors "
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
            if (app.smoke && SmokeNoPromptGuard::shown)
                throw std::runtime_error("Closing or exiting displayed an unsaved-changes prompt.");
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
    resetPreview();
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
