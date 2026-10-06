#include "graphics.h"
#include "color_picker.h"
#include "settings.h"
#include "commands.h"
#include "test_hooks.h"
#include "capture.h"
#include "clipboard.h"
#include "file_io.h"
#include "test_reports.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <array>
#include <string>
#include <stdexcept>
#include <chrono>
#include <sstream>
#include <random>

using namespace snip;
namespace
{
constexpr wchar_t MainClass[] = L"TigerSnip.Main.1", OverlayClass[] = L"TigerSnip.Capture.1",
                  SettingsClass[] = L"TigerSnip.Settings.1",
                  DiagnosticClass[] = L"TigerSnip.ResizeDiagnostic.1";
constexpr UINT TrayMessage = WM_APP + 20, LaunchMessage = WM_APP + 21;
constexpr UINT CaptureTimer = 1, StatusTimer = 2, SmokeTimer = 3, CopyFlashTimer = 4,
               SizeRepeatTimer = 5, TraceHeartbeatTimer = 6;
constexpr UINT SizeRepeatDelay = 300, SizeRepeatInterval = 35;
constexpr float StatusHeight = 32;
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
    FullScreen,
    ToggleActions,
    ToggleTools,
    ToggleFormatting,
    RenderingSettings,
    HighlightTool,
    InstantSnip,
    CropTool,
    EraserTool,
    FlipCurvedArrow,
    RecentSnips,
    RecentClose,
    RecentNewer,
    RecentOlder,
    AutoCopy,
    ColorFirst = PaletteFirst,
    ShowEditor = 1200,
    CircleStyleMenu = 1300,
    ArrowStyleMenu,
    CheckStyleMenu,
    LineStyleMenu,
    StyleChoiceFirst = 1400,
    TextSizeFirst = 1500,
    TextEditControl = 1600,
    LogoStyleFirst = 1800,
    RecentChoiceFirst = RecentFirst
};
constexpr const wchar_t *LogoStyleNames[] = {
    L"S - White badge",        L"S - Soft watermark",     L"Tiger - White badge",
    L"Tiger - Soft watermark", L"Wordmark - White badge", L"Wordmark - Soft watermark"};
const std::array<Color, 8> Palette = {rgb(239, 68, 68),   rgb(249, 115, 22), rgb(250, 204, 21),
                                      rgb(34, 197, 94),   rgb(14, 165, 233), rgb(168, 85, 247),
                                      rgb(255, 255, 255), rgb(15, 23, 42)};
constexpr std::array<uint8_t, 9> StyleCounts = {1, 1, 3, 5, 6, 3, 4, 1, 1};
constexpr int StyleChoiceStride = 8;
constexpr const wchar_t *ToolNames[] = {L"Select", L"Pen",       L"Circle", L"Arrow",    L"Check",
                                        L"Line",   L"Rectangle", L"Text",   L"Highlight"};
constexpr std::array<int, 10> FontSizes = {12, 16, 20, 24, 32, 40, 48, 64, 96, 144};
constexpr std::array<const wchar_t *, 10> WelcomeMessages = {
    L"The snipping tool of your dreams!",
    L"Small snip. Big possibilities.",
    L"Big ideas. Small screenshots.",
    L"A little snip goes a long way.",
    L"Ready, set, snip!",
    L"Make your point. Add an arrow.",
    L"Your ideas look good in pixels.",
    L"A clearer picture. A smoother workday.",
    L"Small snips. Happier teammates.",
    L"One snip closer to \"Got it!\""};
constexpr int styleCommand(Tool tool, int style)
{
    return StyleChoiceFirst +
           (static_cast<int>(tool) - static_cast<int>(Tool::Circle)) * StyleChoiceStride + style;
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
    Pan,
    Crop,
    Erase
};
struct RecentSnip
{
    // Only inactive captures own the full editing state; the active one lives in app.
    Bitmap image, cropSource, thumbnail;
    Com<ID2D1Bitmap> displayThumbnail;
    Document document;
    std::optional<Rect> appliedCrop;
    View view;
    Rect viewport;
    bool fit = true, dirty = false;
    float dpi = 1;
    std::wstring savePath;
    SYSTEMTIME captured{};
    unsigned sequence = 0;
};
constexpr size_t RecentLimit = 10;
struct Application
{
    bool resizeTest = false, resizeTestIdle = false, softwareRendering = false,
         rendererSpecified = false, diagnosticInstance = false;
    std::ofstream resizeTrace;
    struct PaintTiming
    {
        double layout = 0, background = 0, content = 0, present = 0;
    } paintTiming;
    unsigned resizeTestPaints = 0;
    HINSTANCE instance = nullptr;
    HWND window = nullptr, overlay = nullptr, settingsWindow = nullptr, hotkeyControl = nullptr,
         instantHotkeyControl = nullptr, tooltip = nullptr;
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
    Com<ID2D1BitmapBrush> workspaceBrush;
    float workspaceBrushDpiX = 0, workspaceBrushDpiY = 0;
    Com<ID2D1Bitmap> displayBitmap;
    Bitmap previewImage;
    std::vector<Annotation> previewItems;
    ExportOptions previewOptions;
    int previewEditingText = -1;
    bool previewValid = false;
    Bitmap image, desktop, dimDesktop;
    Bitmap cropSource;
    std::optional<Rect> appliedCrop;
    bool cropping = false;
    Point cropEnd;
    Bitmap pickerImage;
    bool pickingColor = false;
    HCURSOR penCursor = nullptr, eraserCursor = nullptr;
    float eraserCursorDpi = 0;
    HCURSOR grabCursor[2] = {};
    float grabCursorDpi = 0;
    float penCursorDiameter = 0;
    Color penCursorColor = 0;
    Tool penCursorTool = Tool::Pen;
    Document document;
    std::vector<RecentSnip> recent;
    int activeRecent = -1, recentScroll = 0, recentFocus = 0;
    bool recentOpen = false;
    unsigned recentSequence = 0;
    Tool tool = Tool::Select;
    bool erasing = false;
    std::array<Color, 9> colors = {Palette[0], Palette[0], Palette[0], Palette[0], Palette[3],
                                   Palette[0], Palette[0], Ink,        Palette[2]};
    std::array<uint8_t, 9> styles{};
    std::vector<Color> palette{Palette.begin(), Palette.end()};
    bool paletteDirty = false;
    Tool geometryTool = Tool::Circle;
    float fontSize = 24;
    bool textBold = false, textBox = false;
    float thickness = 4, highlightWidth = 24, dpi = 1;
    View view;
    Rect viewViewport;
    bool fit = true, dirty = false, capturePending = false, exiting = false, tray = false,
         spaceDown = false;
    bool selecting = false, changed = false, smoke = false;
    bool toolPreferencesDirty = false, shortcutsDirty = false, rendererPreferencesDirty = false;
    std::string preferenceError;
    bool autoCopy = true;
    bool exportPreferencesDirty = false;
    ULONGLONG copyFlashStarted = 0;
    HMENU logoMenu = nullptr;
    HMENU professionalMenu = nullptr;
    unsigned collapsedRows = 0;
    bool layoutPreferencesDirty = false, fullScreen = false, interactiveResize = false;
    WINDOWPLACEMENT windowedPlacement{};
    LONG_PTR windowedStyle = 0;
    HMENU windowedMenu = nullptr;
    Drag drag = Drag::None;
    Point dragStart, panStart;
    Annotation before;
    int handle = -1, virtualX = 0, virtualY = 0;
    POINT selectionStart{}, selectionEnd{};
    std::wstring iniPath, savePath, saveFolder, status;
    size_t welcomeMessage = WelcomeMessages.size();
    WORD hotkey = MAKEWORD('S', HOTKEYF_CONTROL | HOTKEYF_ALT);
    WORD instantHotkey = MAKEWORD('F', HOTKEYF_CONTROL | HOTKEYF_ALT);
    int hotkeyId = 1;
    int instantHotkeyId = 3;
    bool hotkeyRegistered = false;
    bool instantHotkeyRegistered = false;
    std::vector<Button> buttons;
    int hover = 0, pressed = 0;
    int sizeRepeatCommand = 0;
    bool sizeRepeated = false, sizeRepeatUndo = false;
    size_t tooltipCount = 0;
    UINT taskbarCreated = 0;
} app;

class ResizeTrace
{
    const char *name;
    UINT message;
    std::chrono::steady_clock::time_point start;
    bool enabled;
    static inline unsigned depth = 0;
    static inline std::ostringstream pending;

  public:
    explicit ResizeTrace(const char *operation, UINT windowMessage = 0)
        : name(operation), message(windowMessage), enabled(app.resizeTrace.is_open())
    {
        if (enabled)
        {
            ++depth;
            start = std::chrono::steady_clock::now();
        }
    }
    ~ResizeTrace() noexcept
    {
        try
        {
            if (!enabled)
                return;
            const auto now = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - start).count();
            if (name || ms >= 100)
                pending << GetTickCount64() << " " << (name ? name : "WM_OTHER") << " " << ms
                        << "ms depth=" << depth << " message=" << message << "\n";
            if (--depth == 0)
            {
                // Batch diagnostic I/O after the outer operation has finished painting.
                const auto entries = pending.str();
                if (!entries.empty())
                {
                    app.resizeTrace << entries;
                    app.resizeTrace.flush();
                }
                pending.str("");
            }
        }
        catch (...)
        {
            depth = 0;
            OutputDebugStringW(L"Tiger Snip: resize tracing failed.\n");
        }
    }
};

LRESULT CALLBACK mainProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK overlayProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK settingsProcedure(HWND, UINT, WPARAM, LPARAM);
void command(int id);
void stopSizeRepeat();
void finishDrag(bool cancel);
void startSnip(bool instant = false, bool allMonitors = false);
void hideEditorForCapture();
void openOverlay();
void refreshEditorCursor();
void finishTextEditing(bool cancel = false, bool selectAfter = true);
void syncTextEditor();
void beginTextEditing(Point point, int existing = -1);
void selectTool(Tool tool);
void buildButtons();
void refreshRecentThumbnail();
void stashRecentSnip();
void restoreRecentSnip(int index);
void closeRecent();
bool recentPanelCommand(int id);
Rect recentPanelRect();
bool textMode();
Rect clientDips();
bool paletteCommand(int id)
{
    return id >= ColorFirst && static_cast<size_t>(id - ColorFirst) < app.palette.size();
}
int paletteColumns()
{
    // Reserve text formatting even for other tools, so changing tools or selection
    // never changes palette wrapping or moves the screenshot during a drag.
    return std::max(2, static_cast<int>((clientDips().right - 70 - 64 - 264 - 140 - 30) / 34));
}
void repaint()
{
    if (app.window)
    {
        InvalidateRect(app.window, nullptr, FALSE);
        refreshEditorCursor();
    }
}
void error(HWND owner, const char *text) noexcept
{
    showError(owner, text);
}
void status(const std::wstring &text)
{
    app.status = text;
    // If the optional expiry timer fails, keep the message visible until the next action.
    if (!SetTimer(app.window, StatusTimer, 4500, nullptr))
        OutputDebugStringW(L"Tiger Snip: status expiry timer unavailable.\n");
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
    if (app.collapsedRows & (1U << row))
        return 20;
    const int rows =
        (static_cast<int>(app.palette.size()) + 2 + paletteColumns() - 1) / paletteColumns();
    return heights[row] + (row == 2 ? 34.0f * (rows - 1) : 0);
}
float rowTop(int row)
{
    float top = 0;
    for (int i = 0; i < row; ++i)
        top += rowHeight(i);
    return top;
}
float toolbarHeight()
{
    return app.fullScreen ? 0 : rowTop(3);
}
Rect workspaceRect()
{
    auto r = clientDips();
    return {0, toolbarHeight(), r.right, std::max(toolbarHeight(), r.bottom - StatusHeight)};
}
Rect canvasRect()
{
    return workspaceRect();
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
void resetRecentDisplays()
{
    for (auto &snip : app.recent)
        snip.displayThumbnail.reset();
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
bool curvedArrowSelected()
{
    return selected() && app.document.items[app.document.selected].kind == Tool::Arrow &&
           app.document.items[app.document.selected].style == 2;
}
bool curvedArrowCommand(int id)
{
    return id == FlipCurvedArrow;
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
bool highlightMode()
{
    return selected() ? app.document.items[app.document.selected].kind == Tool::Highlight
                      : app.tool == Tool::Highlight;
}
float brushWidth()
{
    return highlightMode() ? app.highlightWidth : app.thickness;
}
void loadToolPreferences()
{
    app.palette = loadPalette(app.iniPath, {Palette.begin(), Palette.end()});
    app.paletteDirty = false;
    app.autoCopy = preferenceUInt(app.iniPath, L"Settings", L"AutoCopy", 1) != 0;
    if (!app.rendererSpecified)
        app.softwareRendering =
            preferenceUInt(app.iniPath, L"Settings", L"SoftwareRendering", 0) != 0;
    app.collapsedRows = preferenceUInt(app.iniPath, L"Settings", L"CollapsedRows", 0) & 7U;
    app.layoutPreferencesDirty = false;
    wchar_t folder[32768]{};
    GetPrivateProfileStringW(L"Settings", L"SaveFolder", L"", folder, 32768, app.iniPath.c_str());
    app.saveFolder = folder;
    app.exportOptions.professionalBorder =
        preferenceUInt(app.iniPath, L"Settings", L"ProfessionalBorder", 0) != 0;
    app.exportOptions.professionalBlur =
        preferenceUInt(app.iniPath, L"Settings", L"ProfessionalBlur", 1) != 0;
    app.exportOptions.professionalRounded =
        preferenceUInt(app.iniPath, L"Settings", L"ProfessionalRounded", 1) != 0;
    app.exportOptions.samtecLogo = preferenceUInt(app.iniPath, L"Settings", L"SamtecLogo", 0) != 0;
    const UINT logoStyle = preferenceUInt(app.iniPath, L"Settings", L"SamtecLogoStyle", 0);
    app.exportOptions.samtecStyle = logoStyle < 6 ? static_cast<uint8_t>(logoStyle) : 0;
    app.exportPreferencesDirty = false;
    for (size_t i = 0; i < app.colors.size(); ++i)
    {
        const std::wstring colorKey = std::wstring(ToolNames[i]) + L"Color";
        const std::wstring styleKey = std::wstring(ToolNames[i]) + L"Style";
        const UINT value =
            preferenceUInt(app.iniPath, L"ToolPreferences", colorKey.c_str(), app.colors[i]);
        const UINT style =
            preferenceUInt(app.iniPath, L"ToolPreferences", styleKey.c_str(), app.styles[i]);
        if (value <= 0xFFFFFF)
            app.colors[i] = value;
        if (style < StyleCounts[i])
            app.styles[i] = static_cast<uint8_t>(style);
    }
    const UINT fontSize = preferenceUInt(app.iniPath, L"ToolPreferences", L"TextFontSize", 24);
    app.fontSize = static_cast<float>(std::clamp(fontSize, 8U, 144U));
    app.thickness = static_cast<float>(
        std::clamp(preferenceUInt(app.iniPath, L"ToolPreferences", L"StrokeWidth", 4), 1U, 100U));
    app.highlightWidth = static_cast<float>(std::clamp(
        preferenceUInt(app.iniPath, L"ToolPreferences", L"HighlightWidth", 24), 4U, 80U));
    app.textBold = preferenceUInt(app.iniPath, L"ToolPreferences", L"TextBold", 0) != 0;
    app.textBox = preferenceUInt(app.iniPath, L"ToolPreferences", L"TextBox", 0) != 0;
    app.geometryTool =
        preferenceUInt(app.iniPath, L"ToolPreferences", L"GeometryTool",
                       static_cast<int>(Tool::Circle)) == static_cast<int>(Tool::Rectangle)
            ? Tool::Rectangle
            : Tool::Circle;
    app.toolPreferencesDirty = false;
}
bool saveToolPreferences()
{
    try
    {
        std::vector<Setting> changes;
        std::vector<SettingsSection> sections;
        auto setting = [&](const wchar_t *section, const std::wstring &key, unsigned value) {
            changes.push_back({section, key, std::to_wstring(value)});
        };
        if (app.paletteDirty)
        {
            SettingsSection palette{L"Palette", {{L"Count", std::to_wstring(app.palette.size())}}};
            for (size_t i = 0; i < app.palette.size(); ++i)
                palette.values.emplace_back(std::to_wstring(i), std::to_wstring(app.palette[i]));
            sections.push_back(std::move(palette));
        }
        if (app.layoutPreferencesDirty)
            setting(L"Settings", L"CollapsedRows", app.collapsedRows);
        if (app.exportPreferencesDirty)
        {
            setting(L"Settings", L"ProfessionalBorder", app.exportOptions.professionalBorder);
            setting(L"Settings", L"ProfessionalBlur", app.exportOptions.professionalBlur);
            setting(L"Settings", L"ProfessionalRounded", app.exportOptions.professionalRounded);
            setting(L"Settings", L"SamtecLogo", app.exportOptions.samtecLogo);
            setting(L"Settings", L"SamtecLogoStyle", app.exportOptions.samtecStyle);
        }
        if (app.rendererPreferencesDirty)
            setting(L"Settings", L"SoftwareRendering", app.softwareRendering);
        if (app.shortcutsDirty)
        {
            setting(L"Settings", L"Hotkey", app.hotkey);
            setting(L"Settings", L"InstantHotkey", app.instantHotkey);
        }
        if (app.toolPreferencesDirty)
        {
            for (size_t i = 0; i < app.colors.size(); ++i)
            {
                setting(L"ToolPreferences", std::wstring(ToolNames[i]) + L"Color", app.colors[i]);
                setting(L"ToolPreferences", std::wstring(ToolNames[i]) + L"Style", app.styles[i]);
            }
            setting(L"ToolPreferences", L"TextFontSize", static_cast<unsigned>(app.fontSize));
            setting(L"ToolPreferences", L"StrokeWidth", static_cast<unsigned>(app.thickness));
            setting(L"ToolPreferences", L"HighlightWidth",
                    static_cast<unsigned>(app.highlightWidth));
            setting(L"ToolPreferences", L"TextBold", app.textBold);
            setting(L"ToolPreferences", L"TextBox", app.textBox);
            setting(L"ToolPreferences", L"GeometryTool", static_cast<unsigned>(app.geometryTool));
        }
        commitPreferences(app.iniPath, changes, sections);
        app.paletteDirty = app.layoutPreferencesDirty = app.exportPreferencesDirty = false;
        app.toolPreferencesDirty = app.shortcutsDirty = app.rendererPreferencesDirty = false;
        app.preferenceError.clear();
        return true;
    }
    catch (const std::exception &failure)
    {
        app.preferenceError = failure.what();
        return false;
    }
}
void saveToolPreferencesOrNotify()
{
    if (!saveToolPreferences())
        error(app.window, app.preferenceError.c_str());
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
    if (key == VK_PAUSE)
        result += L"Pause";
    else if (key >= 'A' && key <= 'Z')
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
bool registerShortcuts(WORD area, WORD full, bool notify = true)
{
    auto reject = [&](const wchar_t *message) {
        if (notify)
            MessageBoxW(app.settingsWindow ? app.settingsWindow : app.window, message,
                        L"Choose different shortcuts", MB_OK | MB_ICONINFORMATION);
        return false;
    };
    for (WORD value : {area, full})
    {
        BYTE key = LOBYTE(value), flags = HIBYTE(value);
        if (key &&
            ((!(flags & (HOTKEYF_CONTROL | HOTKEYF_ALT)) && key != VK_PAUSE) || key == VK_F12))
            return reject(L"Use Ctrl or Alt, or Pause by itself. F12 is reserved by Windows.");
    }
    if (LOBYTE(area) && LOBYTE(full) && LOBYTE(area) == LOBYTE(full) &&
        hotkeyModifiers(area) == hotkeyModifiers(full))
        return reject(L"Area selection and instant capture need different shortcuts.");

    // Release both before registering so the user can swap the two combinations.
    // Roll back both on failure, leaving the previous settings working.
    if (app.hotkeyRegistered)
        UnregisterHotKey(app.window, app.hotkeyId);
    if (app.instantHotkeyRegistered)
        UnregisterHotKey(app.window, app.instantHotkeyId);
    const int areaId = app.hotkeyId == 1 ? 2 : 1;
    const int fullId = app.instantHotkeyId == 3 ? 4 : 3;
    const bool areaOK =
        !LOBYTE(area) || RegisterHotKey(app.window, areaId, hotkeyModifiers(area), LOBYTE(area));
    const bool fullOK =
        areaOK &&
        (!LOBYTE(full) || RegisterHotKey(app.window, fullId, hotkeyModifiers(full), LOBYTE(full)));
    if (!fullOK)
    {
        if (areaOK && LOBYTE(area))
            UnregisterHotKey(app.window, areaId);
        if (app.hotkeyRegistered)
            app.hotkeyRegistered =
                RegisterHotKey(app.window, app.hotkeyId, hotkeyModifiers(app.hotkey),
                               LOBYTE(app.hotkey)) != FALSE;
        if (app.instantHotkeyRegistered)
            app.instantHotkeyRegistered =
                RegisterHotKey(app.window, app.instantHotkeyId, hotkeyModifiers(app.instantHotkey),
                               LOBYTE(app.instantHotkey)) != FALSE;
        return reject(
            L"A shortcut is already in use or reserved by Windows. Choose another combination.");
    }
    app.hotkey = area;
    app.instantHotkey = full;
    app.hotkeyId = areaId;
    app.instantHotkeyId = fullId;
    app.hotkeyRegistered = LOBYTE(area) != 0;
    app.instantHotkeyRegistered = LOBYTE(full) != 0;
    return true;
}
bool startupEnabled()
{
    return snip::startupEnabled(executablePath());
}
void toggleStartup()
{
    const auto path = executablePath();
    const bool enabled = snip::startupEnabled(path);
    setStartupEnabled(path, !enabled);
    status(enabled ? L"Run at sign-in disabled" : L"Tiger Snip will start quietly at sign-in");
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
    wcscpy_s(data.szTip, L"Tiger Snip - click to snip; right-click for menu");
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
void chooseWelcomeMessage()
{
    static std::mt19937 random(static_cast<unsigned>(GetTickCount64() ^ GetCurrentProcessId()));
    const bool previous = app.welcomeMessage < WelcomeMessages.size();
    std::uniform_int_distribution<size_t> choice(0, WelcomeMessages.size() - (previous ? 2 : 1));
    size_t next = choice(random);
    if (previous && next >= app.welcomeMessage)
        ++next;
    app.welcomeMessage = next;
}
void showEditor()
{
    if (!hasImage())
        chooseWelcomeMessage();
    // Undo capture-time cloaking before restoring the editor.
    const BOOL uncloaked = FALSE;
    DwmSetWindowAttribute(app.window, DWMWA_CLOAK, &uncloaked, sizeof(uncloaked));
    ShowWindow(app.window, SW_RESTORE);
    SetForegroundWindow(app.window);
    repaint();
}
void updateTitle()
{
    std::wstring title = app.diagnosticInstance ? L"Tiger Snip - Resize diagnostic" : L"Tiger Snip";
    if (hasImage())
        title += L"  |  " + std::to_wstring(app.image.width) + L" x " +
                 std::to_wstring(app.image.height) + (app.dirty ? L"  *" : L"");
    SetWindowTextW(app.window, title.c_str());
}
void releaseImage()
{
    closeRecent();
    app.activeRecent = -1;
    stopSizeRepeat();
    finishDrag(true);
    KillTimer(app.window, CopyFlashTimer);
    app.copyFlashStarted = 0;
    finishTextEditing(true);
    app.pickingColor = false;
    app.pickerImage = {};
    app.document.clear();
    app.image = {};
    app.cropSource = {};
    app.appliedCrop.reset();
    app.cropping = false;
    app.erasing = false;
    resetPreview();
    app.savePath.clear();
    app.dirty = false;
    app.fit = true;
    app.view = {};
    app.viewViewport = {};
    app.workspaceBrush.reset();
    resetRecentDisplays();
    app.target.reset();
    app.status.clear();
    updateTitle();
}
Bitmap recentThumbnail(const Bitmap &source)
{
    const float scale = std::min({1.0f, 400.0f / source.width, 224.0f / source.height});
    auto thumb = Bitmap::create(std::max(1, static_cast<int>(std::round(source.width * scale))),
                                std::max(1, static_cast<int>(std::round(source.height * scale))));
    for (int y = 0; y < thumb.height; ++y)
        for (int x = 0; x < thumb.width; ++x)
        {
            const float sx =
                std::clamp((x + .5f) * source.width / thumb.width - .5f, 0.0f, source.width - 1.0f);
            const float sy = std::clamp((y + .5f) * source.height / thumb.height - .5f, 0.0f,
                                        source.height - 1.0f);
            const int left = static_cast<int>(sx), top = static_cast<int>(sy);
            const float fx = sx - left, fy = sy - top;
            float channels[4]{};
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                {
                    const size_t p =
                        (static_cast<size_t>(std::min(top + dy, source.height - 1)) * source.width +
                         std::min(left + dx, source.width - 1)) *
                        4;
                    const float weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                    channels[3] += source.pixels[p + 3] * weight;
                    for (int c = 0; c < 3; ++c)
                        channels[c] +=
                            source.pixels[p + c] * source.pixels[p + 3] / 255.0f * weight;
                }
            const size_t out = (static_cast<size_t>(y) * thumb.width + x) * 4;
            for (int c = 0; c < 3; ++c)
                thumb.pixels[out + c] =
                    channels[3] > 0 ? static_cast<uint8_t>(std::clamp(
                                          std::lround(channels[c] * 255 / channels[3]), 0L, 255L))
                                    : 0;
            thumb.pixels[out + 3] = static_cast<uint8_t>(std::lround(channels[3]));
        }
    return thumb;
}
void refreshRecentThumbnail()
{
    if (hasImage() && app.activeRecent >= 0 &&
        app.activeRecent < static_cast<int>(app.recent.size()))
    {
        auto &snip = app.recent[app.activeRecent];
        snip.thumbnail = recentThumbnail(previewImage());
        snip.displayThumbnail.reset();
    }
}
void stashRecentSnip()
{
    if (!hasImage() || app.activeRecent < 0 ||
        app.activeRecent >= static_cast<int>(app.recent.size()))
        return;
    finishDrag(true);
    finishTextEditing();
    refreshRecentThumbnail();
    auto &saved = app.recent[app.activeRecent];
    saved.image = std::move(app.image);
    saved.cropSource = std::move(app.cropSource);
    saved.document = std::move(app.document);
    saved.appliedCrop = app.appliedCrop;
    saved.view = app.view;
    saved.viewport = app.viewViewport;
    saved.fit = app.fit;
    saved.dirty = app.dirty;
    saved.dpi = app.dpi;
    saved.savePath = std::move(app.savePath);
    app.activeRecent = -1;
}
void restoreRecentSnip(int index)
{
    if (index < 0 || index >= static_cast<int>(app.recent.size()) || app.capturePending ||
        app.overlay)
        return;
    closeRecent();
    if (index == app.activeRecent)
        return;
    stashRecentSnip();
    releaseImage();
    auto &saved = app.recent[index];
    app.image = std::move(saved.image);
    app.cropSource = std::move(saved.cropSource);
    app.document = std::move(saved.document);
    app.appliedCrop = saved.appliedCrop;
    app.view = saved.view;
    if (saved.dpi != app.dpi)
    {
        const Point center{(saved.viewport.left + saved.viewport.right) / 2,
                           (saved.viewport.top + saved.viewport.bottom) / 2};
        const auto focus = saved.view.toImage(center);
        app.view.scale *= saved.dpi / app.dpi;
        app.view.origin = center - focus * app.view.scale;
    }
    app.viewViewport = saved.viewport;
    app.fit = saved.fit;
    app.dirty = saved.dirty;
    app.savePath = std::move(saved.savePath);
    app.activeRecent = index;
    app.tool = Tool::Select;
    app.spaceDown = false;
    updateTitle();
    buildButtons();
    repaint();
}
void closeRecent()
{
    if (app.recentOpen)
    {
        app.recentOpen = false;
        app.hover = 0;
        repaint();
    }
}
Rect navigationRect()
{
    auto r = canvasRect();
    const float x = std::min(20.0f, std::max(0.0f, (r.width() - 1) / 2));
    const float y = std::min(20.0f, std::max(0.0f, (r.height() - 1) / 2));
    return {r.left + x, r.top + y, r.right - x, r.bottom - y};
}
Rect imageContentRect()
{
    const float p = static_cast<float>(previewPadding());
    return {-p, -p, app.image.width + p, app.image.height + p};
}
bool canPanImage()
{
    const auto r = navigationRect(), content = imageContentRect();
    return hasImage() && (content.width() * app.view.scale > r.width() + .01f ||
                          content.height() * app.view.scale > r.height() + .01f);
}
void updateView()
{
    if (!hasImage())
        return;
    const auto r = navigationRect(), content = imageContentRect();
    const float minimum = View::fittedScale(r, content, 1 / app.dpi);
    if (!app.fit && app.viewViewport.width() > 0 && app.viewViewport != r)
    {
        const auto old = app.viewViewport;
        const Point focus =
            app.view.toImage({(old.left + old.right) / 2, (old.top + old.bottom) / 2});
        app.view.origin =
            Point{(r.left + r.right) / 2, (r.top + r.bottom) / 2} - focus * app.view.scale;
    }
    app.fit = app.fit || app.view.scale <= minimum + .00001f;
    if (app.fit)
        app.view.fitTo(r, content, 1 / app.dpi);
    else
    {
        app.view.scale = std::clamp(app.view.scale, minimum, 8 / app.dpi);
        app.view.constrain(r, content);
    }
    app.viewViewport = r;
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
    updateView();
    repaint();
    buildButtons();
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
    // Fractional DPI transforms can leave an integer width a few ULPs above its
    // actual size; keep the cursor bounds stable across display scales.
    const int extent = static_cast<int>(std::ceil(diameter - .0001f)) + 6;
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
                bitmap.pixels[offset + channel] =
                    static_cast<uint8_t>(std::round(255 * (outer - black) + component * fill));
            }
            bitmap.pixels[offset + 3] = static_cast<uint8_t>(std::round(255 * outer));
        }
    return bitmap;
}
Bitmap chiselCursorPixels(float width, Color value)
{
    width = std::max(1.0f, width);
    const int extent = static_cast<int>(std::ceil(width)) + 6;
    auto bitmap = Bitmap::create(extent, extent);
    const Point center{extent / 2 + .5f, extent / 2 + .5f};
    const auto hull = chiselSegment(center, center, width);
    for (int y = 0; y < extent; ++y)
        for (int x = 0; x < extent; ++x)
        {
            Point p{x + .5f, y + .5f};
            float distance = width + 6;
            bool inside = true;
            for (size_t i = 0; i < hull.size(); ++i)
            {
                Point a = hull[i], b = hull[(i + 1) % hull.size()];
                auto edge = b - a, delta = p - a;
                inside &= edge.x * delta.y - edge.y * delta.x >= 0;
                distance = std::min(distance, segmentDistance(p, a, b));
            }
            if (!inside)
                distance = -distance;
            // A single dark outline avoids the white fringe over screenshot content.
            // These are premultiplied pixels for the native alpha cursor.
            const float outer = std::clamp(distance + 1.5f, 0.0f, 1.0f);
            const float fill = std::clamp(distance + .5f, 0.0f, 1.0f);
            const size_t offset = (static_cast<size_t>(y) * extent + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                bitmap.pixels[offset + channel] = static_cast<uint8_t>(std::round(
                    32 * (outer - fill) + ((value >> ((2 - channel) * 8)) & 255) * fill));
            bitmap.pixels[offset + 3] = static_cast<uint8_t>(std::round(255 * outer));
        }
    return bitmap;
}
HCURSOR alphaCursor(const Bitmap &pixels, DWORD hotspotX, DWORD hotspotY)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = pixels.width;
    info.bmiHeader.biHeight = -pixels.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const int maskStride = ((pixels.width + 15) / 16) * 2;
    std::vector<uint8_t> maskBits(maskStride * pixels.height, 0);
    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x)
            if (!pixels.pixels[(static_cast<size_t>(y) * pixels.width + x) * 4 + 3])
                maskBits[y * maskStride + x / 8] |= 0x80 >> (x % 8);
    HBITMAP mask = CreateBitmap(pixels.width, pixels.height, 1, 1, maskBits.data());
    HCURSOR cursor = nullptr;
    if (bitmap && bits && mask)
    {
        std::copy(pixels.pixels.begin(), pixels.pixels.end(), static_cast<uint8_t *>(bits));
        // CreateIconIndirect accepts straight-alpha RGB and premultiplies internally.
        // The rasterizers above blend coverage in premultiplied form; convert once here.
        auto nativePixels = static_cast<uint8_t *>(bits);
        for (size_t i = 0; i < pixels.pixels.size(); i += 4)
            if (nativePixels[i + 3])
                for (int c = 0; c < 3; ++c)
                    nativePixels[i + c] = static_cast<uint8_t>(
                        std::min(255, (nativePixels[i + c] * 255 + nativePixels[i + 3] / 2) /
                                          nativePixels[i + 3]));
        ICONINFO icon{};
        icon.xHotspot = hotspotX;
        icon.yHotspot = hotspotY;
        icon.hbmMask = mask;
        icon.hbmColor = bitmap;
        cursor = static_cast<HCURSOR>(CreateIconIndirect(&icon));
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (mask)
        DeleteObject(mask);
    return cursor;
}
HCURSOR currentPenCursor()
{
    const bool highlight = app.tool == Tool::Highlight;
    const float diameter =
        std::max(1.0f, (highlight ? app.highlightWidth : app.thickness) * app.view.scale * app.dpi);
    const Color value = app.colors[static_cast<size_t>(highlight ? Tool::Highlight : Tool::Pen)];
    if (app.penCursor && std::abs(diameter - app.penCursorDiameter) < .001f &&
        value == app.penCursorColor && app.penCursorTool == app.tool)
        return app.penCursor;
    const auto pixels =
        highlight ? chiselCursorPixels(diameter, value) : penCursorPixels(diameter, value);
    HCURSOR cursor = alphaCursor(pixels, pixels.width / 2, pixels.height / 2);
    if (!cursor)
        return LoadCursorW(nullptr, IDC_ARROW);
    HCURSOR previous = app.penCursor;
    app.penCursor = cursor;
    app.penCursorDiameter = diameter;
    app.penCursorColor = value;
    app.penCursorTool = app.tool;
    if (previous)
    {
        if (GetCursor() == previous)
            SetCursor(cursor);
        DestroyCursor(previous);
    }
    return cursor;
}
HCURSOR currentEraserCursor()
{
    if (app.eraserCursor && app.eraserCursorDpi == app.dpi)
        return app.eraserCursor;
    auto pixels = penCursorPixels(12 * app.dpi, rgb(255, 255, 255));
    const float center = pixels.width / 2 + .5f;
    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x)
        {
            const float coverage = std::clamp(
                std::hypot(x + .5f - center, y + .5f - center) - (6 * app.dpi - 1), 0.0f, 1.0f);
            const size_t offset = (static_cast<size_t>(y) * pixels.width + x) * 4;
            for (int channel = 0; channel < 4; ++channel)
                pixels.pixels[offset + channel] =
                    static_cast<uint8_t>(std::round(pixels.pixels[offset + channel] * coverage));
        }
    const auto cursor = alphaCursor(pixels, pixels.width / 2, pixels.height / 2);
    if (!cursor)
        return LoadCursorW(nullptr, IDC_CROSS);
    if (app.eraserCursor)
        DestroyCursor(app.eraserCursor);
    app.eraserCursor = cursor;
    app.eraserCursorDpi = app.dpi;
    return cursor;
}
HCURSOR currentGrabCursor(bool closed)
{
    if (app.grabCursorDpi != app.dpi)
    {
        for (auto &cursor : app.grabCursor)
        {
            if (cursor)
            {
                if (GetCursor() == cursor)
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                DestroyCursor(cursor);
                cursor = nullptr;
            }
        }
        app.grabCursorDpi = app.dpi;
    }
    auto &cursor = app.grabCursor[closed ? 1 : 0];
    if (!cursor)
    {
        // Open palm / gripping fist with transparent antialiased edges.
        const std::vector<Point> outline =
            closed ? std::vector<Point>{{8, 17},  {8, 12},  {10, 10}, {13, 10}, {14, 9},
                                        {17, 9},  {18, 10}, {21, 10}, {22, 12}, {25, 13},
                                        {25, 20}, {22, 25}, {12, 25}, {6, 19},  {6, 17}}
                   : std::vector<Point>{{10, 18}, {10, 9},  {11, 7},  {13, 7},  {14, 9},  {14, 16},
                                        {15, 16}, {15, 5},  {16, 3},  {18, 3},  {19, 5},  {19, 16},
                                        {20, 16}, {20, 7},  {21, 5},  {23, 5},  {24, 7},  {24, 18},
                                        {25, 18}, {25, 11}, {26, 10}, {28, 10}, {29, 12}, {29, 21},
                                        {25, 27}, {14, 27}, {5, 19},  {5, 17},  {7, 15}};
        const float d = app.dpi;
        auto pixels = Bitmap::create(static_cast<int>(std::ceil(32 * d)),
                                     static_cast<int>(std::ceil(32 * d)));
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                const Point p{(x + .5f) / d, (y + .5f) / d};
                bool inside = false;
                float distance = 100;
                for (size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
                {
                    const auto a = outline[j], b = outline[i];
                    if ((a.y > p.y) != (b.y > p.y) &&
                        p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                        inside = !inside;
                    const auto ab = b - a, ap = p - a;
                    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) /
                                                   std::max(.001f, ab.x * ab.x + ab.y * ab.y),
                                               0.0f, 1.0f);
                    distance = std::min(distance, length(p - (a + ab * t)));
                }
                const float signedDistance = (inside ? distance : -distance) * d;
                const float alpha = std::clamp(signedDistance + .5f, 0.0f, 1.0f);
                const float fill = std::clamp(signedDistance - d + .5f, 0.0f, 1.0f);
                const size_t i = (static_cast<size_t>(y) * pixels.width + x) * 4;
                for (int c = 0; c < 3; ++c)
                    pixels.pixels[i + c] =
                        static_cast<uint8_t>(std::round(32 * alpha + 223 * fill));
                pixels.pixels[i + 3] = static_cast<uint8_t>(std::round(255 * alpha));
            }
        cursor = alphaCursor(pixels, static_cast<DWORD>(16 * d), static_cast<DWORD>(16 * d));
    }
    return cursor ? cursor : LoadCursorW(nullptr, IDC_HAND);
}
std::vector<Point> handles(const Annotation &item);
bool handPanAt(Point point)
{
    if (app.tool != Tool::Select || app.erasing || app.cropping || app.pickingColor ||
        !canPanImage() || !canvasRect().contains(point) ||
        !imageContentRect().contains(app.view.toImage(point)))
        return false;
    if (selected())
        for (auto handle : handles(app.document.items[app.document.selected]))
            if (length(point - app.view.toScreen(handle)) <= 9)
                return false;
    return app.document.hit(app.view.toImage(point), 6 / app.view.scale) < 0;
}
bool enabled(int id);
HCURSOR editorCursor(Point point)
{
    const bool onCanvas = hasImage() && canvasRect().contains(point);
    if (app.drag == Drag::Pan || (onCanvas && app.spaceDown && canPanImage()))
        return currentGrabCursor(app.drag == Drag::Pan);
    const bool onButton = std::any_of(app.buttons.begin(), app.buttons.end(), [&](const Button &b) {
        if (app.recentOpen && recentPanelRect().contains(point) && !recentPanelCommand(b.command))
            return false;
        return b.rect.contains(point) && enabled(b.command);
    });
    if (onButton)
        return LoadCursorW(nullptr, IDC_HAND);
    if (app.recentOpen && recentPanelRect().contains(point))
        return LoadCursorW(nullptr, IDC_ARROW);
    if (onCanvas && app.pickingColor)
        return LoadCursorW(nullptr, IDC_CROSS);
    if (onCanvas && app.cropping)
        return LoadCursorW(nullptr, IDC_CROSS);
    if (onCanvas && app.erasing)
        return currentEraserCursor();
    if (onCanvas && handPanAt(point))
        return currentGrabCursor(false);
    if (onCanvas && app.tool == Tool::Text)
        return LoadCursorW(nullptr, IDC_IBEAM);
    if (onCanvas && (app.tool == Tool::Pen || app.tool == Tool::Highlight))
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
            for (float thickness : {1.0f, 4.0f, 40.0f, 100.0f})
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
                    const bool dimensions =
                        GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) &&
                        bitmap.bmWidth == pixels.width && bitmap.bmHeight == pixels.height &&
                        info.xHotspot == static_cast<DWORD>(pixels.width / 2) &&
                        info.yHotspot == static_cast<DWORD>(pixels.height / 2) && !info.fIcon;
                    DeleteObject(info.hbmColor);
                    DeleteObject(info.hbmMask);
                    const size_t center =
                        (static_cast<size_t>(pixels.height / 2) * pixels.width + pixels.width / 2) *
                        4;
                    if (!dimensions ||
                        pixels.sample({static_cast<float>(pixels.width / 2),
                                       static_cast<float>(pixels.height / 2)}) != value ||
                        pixels.pixels[center + 3] != 255 || pixels.pixels[3] != 0 ||
                        currentPenCursor() != cursor)
                        throw std::runtime_error(
                            "Pen cursor size, color, hotspot, or caching failed: thickness=" +
                            std::to_string(thickness) + " zoom=" + std::to_string(zoom) + " dpi=" +
                            std::to_string(dpi) + " bitmap=" + std::to_string(bitmap.bmWidth) +
                            "x" + std::to_string(bitmap.bmHeight) +
                            " expected=" + std::to_string(pixels.width) + " hotspot=" +
                            std::to_string(info.xHotspot) + "," + std::to_string(info.yHotspot));
                }
    DestroyCursor(app.penCursor);
    app.penCursor = nullptr;
    app.dpi = app.view.scale = 1;
    app.thickness = 4;
    app.colors[static_cast<size_t>(Tool::Pen)] = Palette[0];
}
Bitmap compositeCursor(const Bitmap &source, HCURSOR cursor, int x, int y)
{
    auto result = source;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = source.width;
    info.bmiHeader.biHeight = -source.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP surface = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !surface || !bits)
    {
        if (surface)
            DeleteObject(surface);
        if (dc)
            DeleteDC(dc);
        throw std::runtime_error("Cannot allocate native cursor composition test.");
    }
    auto previous = SelectObject(dc, surface);
    std::copy(source.pixels.begin(), source.pixels.end(), static_cast<uint8_t *>(bits));
    const bool drawn = DrawIconEx(dc, x, y, cursor, 0, 0, 0, nullptr, DI_NORMAL) != FALSE;
    GdiFlush();
    std::copy_n(static_cast<uint8_t *>(bits), result.pixels.size(), result.pixels.begin());
    SelectObject(dc, previous);
    DeleteObject(surface);
    DeleteDC(dc);
    if (!drawn)
        throw std::runtime_error("Cannot draw native cursor for visual test.");
    for (size_t i = 3; i < result.pixels.size(); i += 4)
        result.pixels[i] = 255;
    return result;
}
void testChiselCursor()
{
    app.tool = Tool::Highlight;
    for (float dpi : {1.0f, 1.5f, 2.0f})
        for (float zoom : {.1f, 1.0f, 8.0f})
            for (float width : {4.0f, 24.0f, 80.0f})
                for (Color brush : {Palette[2], Palette[6], Palette[7]})
                {
                    app.dpi = dpi;
                    app.view.scale = zoom / dpi;
                    app.highlightWidth = width;
                    app.colors[static_cast<size_t>(Tool::Highlight)] = brush;
                    const auto cursor = currentPenCursor();
                    const auto pixels = chiselCursorPixels(std::max(1.0f, width * zoom), brush);
                    for (Color background : {rgb(8, 16, 24), rgb(245, 240, 225)})
                    {
                        auto source = Bitmap::create(pixels.width, pixels.height);
                        for (size_t i = 0; i < source.pixels.size(); i += 4)
                        {
                            source.pixels[i] = (background >> 16) & 255;
                            source.pixels[i + 1] = (background >> 8) & 255;
                            source.pixels[i + 2] = background & 255;
                            source.pixels[i + 3] = 255;
                        }
                        const auto drawn = compositeCursor(source, cursor, 0, 0);
                        for (size_t i = 0; i < drawn.pixels.size(); i += 4)
                            for (int c = 0; c < 3; ++c)
                            {
                                const int expected =
                                    pixels.pixels[i + c] +
                                    (source.pixels[i + c] * (255 - pixels.pixels[i + 3]) + 127) /
                                        255;
                                if (std::abs(drawn.pixels[i + c] - expected) > 2)
                                {
                                    saveBytes(L"chisel-cursor-failure.png",
                                              app.graphics.png(drawn));
                                    throw std::runtime_error(
                                        "Native chisel cursor alpha mismatch: width=" +
                                        std::to_string(width) + " zoom=" + std::to_string(zoom) +
                                        " dpi=" + std::to_string(dpi) + " pixel=" +
                                        std::to_string(i / 4) + " channel=" + std::to_string(c) +
                                        " actual=" + std::to_string(drawn.pixels[i + c]) +
                                        " expected=" + std::to_string(expected) +
                                        " alpha=" + std::to_string(pixels.pixels[i + 3]));
                                }
                                if (!pixels.pixels[i + 3] &&
                                    drawn.pixels[i + c] != source.pixels[i + c])
                                    throw std::runtime_error(
                                        "Transparent chisel cursor pixels changed the image.");
                            }
                    }
                }
    auto preview = Bitmap::create(720, 260);
    for (int y = 0; y < preview.height; ++y)
        for (int x = 0; x < preview.width; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * preview.width + x) * 4;
            const Color background =
                x < 240   ? rgb(245, 240, 225)
                : x < 480 ? rgb(18, 30, 42)
                          : ((x / 12 + y / 12) % 2 ? rgb(42, 105, 128) : rgb(146, 80, 70));
            preview.pixels[i] = (background >> 16) & 255;
            preview.pixels[i + 1] = (background >> 8) & 255;
            preview.pixels[i + 2] = background & 255;
            preview.pixels[i + 3] = 255;
        }
    app.dpi = app.view.scale = 1;
    app.colors[static_cast<size_t>(Tool::Highlight)] = Palette[2];
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
        {
            app.highlightWidth = row == 0 ? 4 : row == 1 ? 24 : 80;
            const auto cursor = currentPenCursor();
            const auto pixels = chiselCursorPixels(app.highlightWidth, Palette[2]);
            preview = compositeCursor(preview, cursor, column * 240 + 120 - pixels.width / 2,
                                      row * 80 + 40 - pixels.height / 2);
        }
    saveBytes(L"chisel-cursor-preview.png", app.graphics.png(preview));
    app.tool = Tool::Select;
    app.highlightWidth = 24;
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
bool recentChoice(int id)
{
    return id >= RecentChoiceFirst && id < RecentChoiceFirst + static_cast<int>(app.recent.size());
}
bool recentPanelCommand(int id)
{
    return recentChoice(id) || id == RecentClose || id == RecentNewer || id == RecentOlder;
}
int recentVisibleRows()
{
    const float top = app.fullScreen ? 8 : (!(app.collapsedRows & 1) ? 55 : 28);
    return std::clamp(static_cast<int>((clientDips().bottom - StatusHeight - top - 90) / 112), 1,
                      5);
}
Rect recentPanelRect()
{
    const auto client = clientDips();
    const int rows =
        std::min(recentVisibleRows(), std::max(1, (static_cast<int>(app.recent.size()) + 1) / 2));
    const float width = std::min(384.0f, client.width() - 16), height = 82 + rows * 112.0f;
    const float top = app.fullScreen ? client.bottom - StatusHeight - height - 8
                                     : (!(app.collapsedRows & 1) ? 55 : 28);
    return {client.right - width - 8, top, client.right - 8, top + height};
}
ToolbarLayout toolbarLayout()
{
    const float width = clientDips().right;
    const float formattingWidth = textMode() ? 264 : 160;
    // Center the whole label/control group between the eyedropper and zoom controls.
    const float paletteRight =
        70 + 34 * std::min(static_cast<int>(app.palette.size()) + 2, paletteColumns());
    const float formattingLeft = (paletteRight + width - 140 - formattingWidth) / 2;
    return {{20, rowTop(1) + 15, width >= 980 ? 466.0f : 318.0f, rowTop(1) + 59},
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
        x = 344;
        add(CropTool, L"Crop", 82, 11);
        x = clientDips().right - 350;
        add(RecentSnips, L"Recent", 114, 11);
        x = clientDips().right - 226;
        add(Copy, L"Copy", 114, 11);
        x += 6;
        add(Save, L"Save", 82, 11);
    }
    if (!app.fullScreen && !(app.collapsedRows & 2))
    {
        x = layout.draw.left + 8;
        const bool wide = clientDips().right >= 980;
        add(SelectTool, L"Select", wide ? 86 : 78, rowTop(1) + 19);
        x += wide ? 8 : 4;
        add(PenTool, L"Pen", wide ? 74 : 64, rowTop(1) + 19);
        x += wide ? 8 : 4;
        add(HighlightTool, wide ? L"Highlight" : L"", wide ? 108 : 36, rowTop(1) + 19);
        x += wide ? 8 : 4;
        add(TextTool, wide ? L"Text" : L"", wide ? 74 : 36, rowTop(1) + 19);
        x += wide ? 8 : 4;
        add(EraserTool, L"", 36, rowTop(1) + 19);
        x = layout.shapes.left + 8;
        addStyleTool(CircleTool, L"Shapes", CircleStyleMenu);
        addStyleTool(ArrowTool, L"Arrow", ArrowStyleMenu);
        addStyleTool(CheckTool, L"Check", CheckStyleMenu);
        addStyleTool(LineTool, L"Line", LineStyleMenu);
    }
    if (!app.fullScreen && !(app.collapsedRows & 4))
    {
        const float y = rowTop(2) + 7;
        const int columns = paletteColumns();
        for (int i = 0; i < static_cast<int>(app.palette.size()) + 2; ++i)
        {
            x = 70 + (i % columns) * 34.0f;
            const float top = y + (i / columns) * 34.0f;
            if (i < static_cast<int>(app.palette.size()))
                app.buttons.push_back({{x, top + 2, x + 24, top + 26}, ColorFirst + i, L""});
            else
                add(i == static_cast<int>(app.palette.size()) ? CustomColor : Eyedropper, L"", 28,
                    top, 28);
        }
        if (!app.erasing)
        {
            x = layout.formatting.left + 52;
            add(SizeDown, L"\u2212", 28, y, 28);
            x += 48;
            add(SizeUp, L"+", 28, y, 28);
        }
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
        add(RecentSnips, L"Recent", 114, y, 24);
    }

    if (!hasImage())
    {
        auto canvas = canvasRect();
        float middle = canvas.top + canvas.height() / 2;
        x = clientDips().right / 2 - 76;
        add(NewSnip, L"Take a snip", 152, middle + 30, 42);
    }

    if (curvedArrowSelected() && app.tool == Tool::Select && app.drag == Drag::None &&
        !app.erasing && !app.cropping && !app.pickingColor && !app.textEdit)
    {
        const auto canvas = canvasRect(), box = app.document.items[app.document.selected].bounds();
        const auto a = app.view.toScreen({box.left, box.top});
        const auto b = app.view.toScreen({box.right, box.bottom});
        constexpr float width = 58, height = 28, margin = 8;
        if (b.x >= canvas.left && a.x <= canvas.right && b.y >= canvas.top &&
            a.y <= canvas.bottom && canvas.width() >= width + margin * 2 &&
            canvas.height() >= height + margin * 2)
        {
            x = std::clamp((a.x + b.x - width) / 2, canvas.left + margin,
                           canvas.right - width - margin);
            const float y =
                std::clamp(a.y - height - 12 >= canvas.top + margin ? a.y - height - 12 : b.y + 12,
                           canvas.top + margin, canvas.bottom - height - margin);
            add(FlipCurvedArrow, L"Flip", width, y, height);
        }
    }

    if (app.recentOpen)
    {
        const auto panel = recentPanelRect();
        const int rows = recentVisibleRows(),
                  totalRows = (static_cast<int>(app.recent.size()) + 1) / 2;
        app.recentScroll = std::clamp(app.recentScroll, 0, std::max(0, totalRows - rows));
        x = panel.right - 40;
        add(RecentClose, L"\u00D7", 28, panel.top + 10, 28);
        const float cellWidth = (panel.width() - 38) / 2;
        for (int slot = 0; slot < rows * 2; ++slot)
        {
            const int index = static_cast<int>(app.recent.size()) - 1 - app.recentScroll * 2 - slot;
            if (index < 0)
                break;
            x = panel.left + 14 + (slot % 2) * (cellWidth + 10);
            add(RecentChoiceFirst + index, L"", cellWidth, panel.top + 48 + (slot / 2) * 112, 104);
        }
        if (totalRows > rows)
        {
            x = panel.right - 144;
            add(RecentNewer, L"Newer", 60, panel.bottom - 30, 24);
            add(RecentOlder, L"Older", 60, panel.bottom - 30, 24);
        }
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
            case CropTool:
                hint = L"Crop (C) - drag the area to keep; Ctrl+Z restores the full image";
                break;
            case Copy:
                hint = L"Copy image with annotations (Ctrl+C)";
                break;
            case Save:
                hint = L"Save PNG (Ctrl+S)";
                break;
            case RecentSnips:
                hint = L"Reopen one of the last 10 snips from this session (Ctrl+Shift+R)";
                break;
            case RecentClose:
                hint = L"Close recent snips (Esc)";
                break;
            case SelectTool:
                hint = L"Select, move, and resize (V)";
                break;
            case PenTool:
                hint = L"Freehand pen (P)";
                break;
            case HighlightTool:
                hint = L"Highlight (H) - translucent chisel brush; color and width are remembered";
                break;
            case EraserTool:
                hint = L"Eraser (E) - click or drag to delete whole strokes and annotations; "
                       L"Ctrl+Z undoes";
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
            case FlipCurvedArrow:
                hint = L"Flip only this arrow's curve to the other side; keep its endpoints";
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
                hint = L"Add a color to your palette";
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
                if (paletteCommand(b.command))
                    hint = L"Use this color; right-click to edit or delete";
                else if (recentChoice(b.command))
                    hint = L"Reopen this snip with its annotations, crop, and undo history";
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
            if (app.tooltipCount)
                SendMessageW(app.tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
        }
        app.tooltipCount = app.buttons.size();
    }
}
bool enabled(int id)
{
    if (id == RecentSnips || recentPanelCommand(id))
    {
        if (app.recent.empty() || app.capturePending || app.overlay)
            return false;
        if (id == RecentNewer)
            return app.recentScroll > 0;
        if (id == RecentOlder)
            return app.recentScroll + recentVisibleRows() <
                   (static_cast<int>(app.recent.size()) + 1) / 2;
        return id == RecentSnips || app.recentOpen;
    }
    if (curvedArrowCommand(id))
        return hasImage() && curvedArrowSelected() && app.tool == Tool::Select && !app.erasing &&
               !app.cropping && !app.pickingColor &&
               length(app.document.items[app.document.selected].b -
                      app.document.items[app.document.selected].a) >= .01f;
    if (id == Undo || id == Redo)
        return hasImage() && (id == Undo ? app.document.canUndo() : app.document.canRedo());
    if (id == NewSnip || id == InstantSnip)
        return !app.capturePending && !app.overlay;
    if (id == Copy || id == Save || id == SaveAs || id == Fit || id == Actual || id == Eyedropper ||
        id == CropTool || id == TextTool || id == HighlightTool || id == EraserTool ||
        id == RectangleTool || id == TextBold || id == TextBox || id == TextSizeMenu ||
        (id >= SelectTool && id <= LineTool) || (id >= CircleStyleMenu && id <= LineStyleMenu))
        return hasImage();
    return true;
}
bool active(int id)
{
    if (id == RecentSnips)
        return app.recentOpen;
    if (id == EraserTool)
        return app.erasing;
    if (app.erasing &&
        (id == TextTool || id == HighlightTool || (id >= SelectTool && id <= LineTool)))
        return false;
    if (id == CropTool)
        return app.cropping;
    if (app.cropping &&
        (id == HighlightTool || id == TextTool || (id >= SelectTool && id <= LineTool)))
        return false;
    if (id == HighlightTool)
        return app.tool == Tool::Highlight;
    if (id == TextTool)
        return app.tool == Tool::Text;
    if (id == CircleTool)
        return app.tool == Tool::Circle || app.tool == Tool::Rectangle;
    if (id == TextBold || id == TextBox)
    {
        const bool current =
            selected() && app.document.items[app.document.selected].kind == Tool::Text;
        return id == TextBold
                   ? (current ? app.document.items[app.document.selected].bold : app.textBold)
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
    ResizeTrace trace("ensureTarget");
    RECT rect{};
    GetClientRect(app.window, &rect);
    const auto size = D2D1::SizeU(std::max(1L, rect.right), std::max(1L, rect.bottom));
    if (app.target)
    {
        const auto previous = app.target->GetPixelSize();
        // Coalesce size messages and resize the backing surface only when painting.
        if (previous.width != size.width || previous.height != size.height)
        {
            HRESULT result;
            {
                ResizeTrace resize("D2D.Resize");
                result = app.target->Resize(size);
            }
            if (result != D2DERR_RECREATE_TARGET)
            {
                check(result, "Cannot resize the editor renderer.");
                return;
            }
            app.target.reset();
        }
        else
            return;
    }
    app.workspaceBrush.reset();
    app.displayBitmap.reset();
    resetRecentDisplays();
    app.graphics.initialize();
    auto properties = D2D1::RenderTargetProperties(
        app.softwareRendering ? D2D1_RENDER_TARGET_TYPE_SOFTWARE : D2D1_RENDER_TARGET_TYPE_DEFAULT);
    properties.dpiX = properties.dpiY = app.dpi * 96;
    check(app.graphics.factory->CreateHwndRenderTarget(
              properties,
              D2D1::HwndRenderTargetProperties(app.window, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
              app.target.put()),
          "Cannot initialize the editor renderer.");
}
Com<ID2D1BitmapBrush> createWorkspaceBrush(ID2D1RenderTarget *rt)
{
    // Render one antialiased dot at the target's DPI, then repeat this 24-DIP tile.
    // Use one drawing call at every window size instead of thousands of ellipses.
    Com<ID2D1BitmapRenderTarget> tile;
    check(rt->CreateCompatibleRenderTarget(D2D1::SizeF(24, 24), tile.put()),
          "Cannot create workspace pattern.");
    Com<ID2D1SolidColorBrush> dot;
    check(tile->CreateSolidColorBrush(color(rgb(222, 225, 236)), dot.put()),
          "Cannot draw workspace pattern.");
    tile->BeginDraw();
    tile->Clear(D2D1::ColorF(0, 0));
    tile->FillEllipse(D2D1::Ellipse({18, 18}, .8f, .8f), dot.get());
    check(tile->EndDraw(), "Cannot finish workspace pattern.");
    Com<ID2D1Bitmap> bitmap;
    check(tile->GetBitmap(bitmap.put()), "Cannot read workspace pattern.");
    Com<ID2D1BitmapBrush> brush;
    check(rt->CreateBitmapBrush(
              bitmap.get(),
              D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP),
              brush.put()),
          "Cannot paint workspace pattern.");
    return brush;
}
bool setSoftwareRendering(bool software)
{
    if (software != app.softwareRendering)
    {
        // Keep the previous renderer available if creating the replacement fails.
        // The screenshot, CPU preview, annotations, history, selection, and view stay intact.
        auto previousTarget = std::move(app.target);
        auto previousPattern = std::move(app.workspaceBrush);
        auto previousDisplay = std::move(app.displayBitmap);
        const bool previousMode = app.softwareRendering;
        app.softwareRendering = software;
        try
        {
            ensureTarget();
        }
        catch (...)
        {
            app.target = std::move(previousTarget);
            app.workspaceBrush = std::move(previousPattern);
            app.displayBitmap = std::move(previousDisplay);
            app.softwareRendering = previousMode;
            throw;
        }
    }
    app.rendererSpecified = false; // An explicit UI choice takes precedence over launch overrides.
    app.rendererPreferencesDirty = true;
    const bool saved = saveToolPreferences();
    status(software ? L"Software rendering enabled" : L"Hardware acceleration enabled");
    UpdateWindow(app.window);
    return saved;
}
void openRenderingSettings(PFTASKDIALOGCALLBACK callback = nullptr, LONG_PTR context = 0)
{
    static constexpr TASKDIALOG_BUTTON buttons[] = {{IDOK, L"Apply"}};
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof(dialog);
    dialog.hwndParent = app.window;
    dialog.hInstance = app.instance;
    dialog.dwFlags = static_cast<TASKDIALOG_FLAGS>(
        TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW |
        (app.softwareRendering ? TDF_VERIFICATION_FLAG_CHECKED : 0) |
        (callback ? TDF_CALLBACK_TIMER : 0));
    dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    dialog.pszWindowTitle = L"Rendering - Tiger Snip";
    dialog.pszMainInstruction = L"Choose how Tiger Snip draws its window";
    dialog.pszContent =
        L"Enable software rendering if Tiger Snip freezes, shows stale content, or takes seconds "
        L"to "
        L"redraw when you resize or maximize the window.\n\n"
        L"Off: hardware acceleration (recommended for most PCs).\n"
        L"On: software rendering, which can help with graphics-driver issues but may use more CPU "
        L"on large displays.\n\n"
        L"Changes apply immediately and are remembered. Copied and saved image quality is "
        L"unchanged.";
    dialog.pszVerificationText = L"Use software rendering (compatibility mode)";
    dialog.pszFooter = L"This setting affects Tiger Snip only.";
    dialog.cButtons = 1;
    dialog.pButtons = buttons;
    dialog.nDefaultButton = IDOK;
    dialog.cxWidth = 340;
    dialog.pfCallback = callback;
    dialog.lpCallbackData = context;
    int choice = IDCANCEL;
    BOOL software = app.softwareRendering;
    check(TaskDialogIndirect(&dialog, &choice, nullptr, &software),
          "Cannot open rendering settings.");
    if (choice == IDOK && !setSoftwareRendering(software != FALSE))
        error(app.window,
              "Rendering changed for this session, but the preference could not be saved. "
              "Check that your personal Tiger Snip settings folder is writable.");
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
    case RecentSnips:
        rt->DrawEllipse(D2D1::Ellipse({origin.x + 11, origin.y + 10}, 7, 7), brush, 1.7f);
        line(4, 2, 4, 7);
        line(4, 7, 9, 7);
        line(11, 6, 11, 10);
        line(11, 10, 15, 12);
        break;
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
    case CropTool:
        line(5, 1, 5, 15);
        line(5, 15, 19, 15);
        line(1, 5, 15, 5);
        line(15, 5, 15, 19);
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
    case HighlightTool:
        line(4, 14, 11, 3);
        line(11, 3, 17, 7);
        line(17, 7, 10, 18);
        line(10, 18, 4, 14);
        line(3, 19, 14, 19);
        break;
    case TextTool:
        line(3, 4, 17, 4);
        line(10, 4, 10, 17);
        line(6, 17, 14, 17);
        break;
    case EraserTool:
        line(3, 12, 11, 3);
        line(11, 3, 18, 9);
        line(18, 9, 10, 18);
        line(10, 18, 7, 18);
        line(7, 18, 3, 14);
        line(3, 14, 3, 12);
        line(7, 8, 14, 14);
        line(10, 18, 19, 18);
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
    ResizeTrace trace(alternate ? "paint.offscreen" : "paint.window");
    using Clock = std::chrono::steady_clock;
    auto phaseStart = app.resizeTest ? Clock::now() : Clock::time_point{};
    auto phase = [&](double &elapsed) {
        if (app.resizeTest)
        {
            auto now = Clock::now();
            elapsed = std::chrono::duration<double, std::milli>(now - phaseStart).count();
            phaseStart = now;
        }
    };
    if (!alternate)
        ensureTarget();
    {
        ResizeTrace layout("paint.layout");
        updateView();
        buildButtons();
    }
    ID2D1RenderTarget *rt = alternate ? alternate : app.target.get();
    auto client = clientDips(), canvas = canvasRect();
    Com<ID2D1Bitmap> alternateBitmap;
    Com<ID2D1BitmapBrush> alternateWorkspaceBrush;
    auto &workspaceBrush = alternate ? alternateWorkspaceBrush : app.workspaceBrush;
    float dpiX = 0, dpiY = 0;
    rt->GetDpi(&dpiX, &dpiY);
    if (!workspaceBrush ||
        (!alternate && (app.workspaceBrushDpiX != dpiX || app.workspaceBrushDpiY != dpiY)))
    {
        workspaceBrush = createWorkspaceBrush(rt);
        if (!alternate)
        {
            app.workspaceBrushDpiX = dpiX;
            app.workspaceBrushDpiY = dpiY;
        }
    }
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
    phase(app.paintTiming.layout);
    rt->BeginDraw();
    rt->Clear(color(rgb(246, 247, 251)));
    rt->PushAxisAlignedClip({canvas.left, canvas.top, canvas.right, canvas.bottom},
                            D2D1_ANTIALIAS_MODE_ALIASED);
    workspaceBrush->SetTransform(D2D1::Matrix3x2F::Translation(0, canvas.top));
    rt->FillRectangle({canvas.left, canvas.top, canvas.right, canvas.bottom}, workspaceBrush.get());
    rt->PopAxisAlignedClip();
    phase(app.paintTiming.background);
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
        if (hasImage() && client.right > 1100)
            text(std::to_wstring(app.image.width) + L" \u00D7 " + std::to_wstring(app.image.height),
                 {client.right - 490, 11, client.right - 362, 47}, Muted,
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
        divider(formattingLeft - 10, formatTop + 11, formatTop + 31);
        if (app.erasing)
            text(L"Whole object",
                 {formattingLeft, formatTop + 5, formattingLeft + 160, formatTop + 37}, Muted,
                 app.graphics.smallFont.get(), true);
        else
        {
            text(textMode()        ? L"Size"
                 : highlightMode() ? L"Width"
                                   : L"Stroke",
                 {formattingLeft, formatTop + 5, formattingLeft + 45, formatTop + 37}, Muted,
                 app.graphics.smallFont.get());
            rounded({formattingLeft + 52, formatTop + 7, formattingLeft + 160, formatTop + 35},
                    rgb(238, 239, 246), 8);
        }
        divider((layout.formatting.right + client.right - 140) / 2, formatTop + 11, formatTop + 31);
        rounded({client.right - 140, formatTop + 7, client.right - 24, formatTop + 35},
                rgb(238, 239, 246), 8);
    }
    for (size_t index = 0; index < app.buttons.size(); ++index)
    {
        const auto &button = app.buttons[index];
        if (curvedArrowCommand(button.command) || recentPanelCommand(button.command))
            continue; // These selection controls are painted above the image below.
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
        if (paletteCommand(button.command))
        {
            Color value = app.palette[button.command - ColorFirst];
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
                                button.command == TextTool || button.command == EraserTool;
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
                             ? app.colors[static_cast<size_t>(Tool::Check)]
                             : fg;
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
                 button.command == TextTool || button.command == HighlightTool ||
                 button.command == EraserTool)
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
        else if (button.command == RecentSnips)
        {
            drawUIIcon(rt, brush.get(), RecentSnips, {r.left + 8, (r.top + r.bottom) / 2 - 10}, fg);
            text(L"Recent", {r.left + 32, r.top, r.right - 26, r.bottom}, fg,
                 app.graphics.smallFont.get());
            rounded({r.right - 23, r.top + 7, r.right - 5, r.bottom - 7}, rgb(255, 255, 255), 4);
            text(std::to_wstring(app.recent.size()), {r.right - 23, r.top, r.right - 5, r.bottom},
                 available ? Accent : Muted, app.graphics.smallFont.get(), true);
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
                     : brushWidth();
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
        if (app.drag == Drag::Crop)
        {
            const auto crop = rectangle(app.dragStart, app.cropEnd);
            const auto a = app.view.toScreen({crop.left, crop.top});
            const auto b = app.view.toScreen({crop.right, crop.bottom});
            const auto top = app.view.toScreen({0, 0});
            const auto bottom = app.view.toScreen(
                {static_cast<float>(app.image.width), static_cast<float>(app.image.height)});
            brush->SetColor(color(rgb(0, 0, 0), .48f));
            rt->FillRectangle({top.x, top.y, bottom.x, a.y}, brush.get());
            rt->FillRectangle({top.x, b.y, bottom.x, bottom.y}, brush.get());
            rt->FillRectangle({top.x, a.y, a.x, b.y}, brush.get());
            rt->FillRectangle({b.x, a.y, bottom.x, b.y}, brush.get());
            brush->SetColor(color(rgb(255, 255, 255)));
            rt->DrawRectangle({a.x, a.y, b.x, b.y}, brush.get(), 2);
            brush->SetColor(color(Accent));
            rt->DrawRectangle({a.x, a.y, b.x, b.y}, brush.get(), 1);
        }
        if (selected() && !app.textEdit && app.drag != Drag::Draw && !app.cropping)
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
        const auto welcome = WelcomeMessages[app.welcomeMessage % WelcomeMessages.size()];
        text(welcome, {20, middle - 43, client.right - 20, middle + 1}, Ink,
             app.graphics.titleFont.get(), true);
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
    for (size_t index = 0; index < app.buttons.size(); ++index)
    {
        const auto &button = app.buttons[index];
        if (!curvedArrowCommand(button.command))
            continue;
        const auto r = button.rect;
        const bool over = app.hover == button.command, available = enabled(button.command);
        const bool down = app.pressed == static_cast<int>(index + 1) && over;
        rounded({r.left, r.top + 2, r.right, r.bottom + 2}, rgb(215, 211, 230), 7);
        panel(r,
              down                ? rgb(219, 211, 248)
              : over && available ? rgb(233, 226, 255)
                                  : rgb(255, 255, 255),
              rgb(199, 190, 227));
        text(button.label, r, available ? Accent : Muted, app.graphics.smallFont.get(), true);
    }
    std::wstring message = app.status;
    if (message.empty())
    {
        if (hasImage())
        {
            const wchar_t *hints[] = {
                L"Select: drag image to pan; drag annotations to move; handles resize",
                L"Pen: drag to draw; Ctrl+Z undoes",
                L"Circle: drag to draw; Shift makes a circle",
                L"Arrow: drag to draw; select and drag endpoints to turn",
                L"Check / X: click to place; drag to size",
                L"Line: drag to draw; Shift snaps angle; drag endpoints to resize",
                L"Rectangle: drag to draw; Shift makes a square",
                L"Text: click and type; Ctrl+Enter finishes; double-click to edit",
                (L"Highlight: drag with the chisel brush; change color or width below; Ctrl+Z "
                 L"undoes")};
            message =
                app.cropping
                    ? L"Crop: drag the area to keep; release to crop; Esc cancels; Ctrl+Z restores"
                : app.pickingColor ? L"Eyedropper: click the image to pick a color; Esc cancels"
                : app.erasing  ? L"Eraser: click or drag to delete whole annotations; Ctrl+Z undoes"
                : app.textEdit ? L"Text: Ctrl+Enter finishes; Enter adds a line; Esc cancels"
                : curvedArrowSelected() ? L"Curved arrow: Flip changes the bend; Ctrl+Z undoes"
                                        : hints[static_cast<int>(app.tool)];
        }
        else
            message = L"Ready when you are";
    }
    brush->SetColor(color(app.status.empty() ? Accent : rgb(42, 169, 106)));
    if (!app.fullScreen)
        rt->FillEllipse(D2D1::Ellipse({22, client.bottom - StatusHeight / 2}, 3, 3), brush.get());
    text(message,
         {app.fullScreen ? 402.0f : 34.0f, client.bottom - StatusHeight,
          client.right - (hasImage() ? 292 : 14), client.bottom},
         Muted, app.graphics.smallFont.get());
    if (hasImage())
        text(std::to_wstring(static_cast<int>(std::round(app.view.scale * app.dpi * 100))) +
                 L"%   \u00B7   " + std::to_wstring(app.image.width) + L" \u00D7 " +
                 std::to_wstring(app.image.height),
             {client.right - 190, client.bottom - StatusHeight, client.right - 12, client.bottom},
             Muted, app.graphics.smallFont.get(), true);
    if (app.recentOpen)
    {
        const auto r = recentPanelRect();
        rounded({r.left - 2, r.top + 5, r.right + 2, r.bottom + 6}, rgb(213, 211, 225), 14);
        panel(r, rgb(255, 255, 255), rgb(224, 222, 236));
        text(L"Recent snips", {r.left + 16, r.top + 10, r.right - 120, r.top + 38}, Ink,
             app.graphics.font.get());
        text(std::to_wstring(app.recent.size()) + L" of 10",
             {r.right - 116, r.top + 10, r.right - 48, r.top + 38}, Muted,
             app.graphics.smallFont.get(), true);
        text(L"Kept until Tiger Snip exits",
             {r.left + 16, r.bottom - 28, r.right - 150, r.bottom - 6}, Muted,
             app.graphics.smallFont.get());
        for (size_t slot = 0; slot < app.buttons.size(); ++slot)
        {
            const auto &button = app.buttons[slot];
            if (!recentPanelCommand(button.command))
                continue;
            const auto cell = button.rect;
            const bool over = app.hover == button.command, available = enabled(button.command);
            const bool down = app.pressed == static_cast<int>(slot + 1) && over;
            if (!recentChoice(button.command))
            {
                rounded(cell,
                        down                ? rgb(219, 211, 248)
                        : over && available ? rgb(242, 238, 255)
                                            : rgb(249, 249, 252),
                        7);
                text(button.label, cell, available ? Ink : Muted, app.graphics.smallFont.get(),
                     true);
                continue;
            }
            const int index = button.command - RecentChoiceFirst;
            auto &snip = app.recent[index];
            const bool current = index == app.activeRecent;
            const bool focus = index == static_cast<int>(app.recent.size()) - 1 - app.recentFocus;
            panel(cell, down || current ? rgb(242, 238, 255) : rgb(255, 255, 255),
                  current || over || focus ? Accent : rgb(226, 227, 237));
            const Rect art{cell.left + 6, cell.top + 6, cell.right - 6, cell.top + 78};
            rounded(art, rgb(247, 248, 252), 5);
            if (!snip.thumbnail.empty())
            {
                Com<ID2D1Bitmap> offscreen;
                auto &display = alternate ? offscreen : snip.displayThumbnail;
                if (!display)
                {
                    auto pixels = snip.thumbnail.pixels;
                    for (size_t i = 0; i < pixels.size(); i += 4)
                        for (int c = 0; c < 3; ++c)
                            pixels[i + c] =
                                static_cast<uint8_t>((pixels[i + c] * pixels[i + 3] + 127) / 255);
                    check(rt->CreateBitmap(D2D1::SizeU(snip.thumbnail.width, snip.thumbnail.height),
                                           pixels.data(), snip.thumbnail.width * 4,
                                           D2D1::BitmapProperties(
                                               D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                 D2D1_ALPHA_MODE_PREMULTIPLIED),
                                               96, 96),
                                           display.put()),
                          "Cannot display a recent snip thumbnail.");
                }
                const float scale = std::min(art.width() / snip.thumbnail.width,
                                             art.height() / snip.thumbnail.height);
                const float width = snip.thumbnail.width * scale,
                            height = snip.thumbnail.height * scale;
                const float left = art.left + (art.width() - width) / 2,
                            top = art.top + (art.height() - height) / 2;
                rt->DrawBitmap(display.get(), {left, top, left + width, top + height}, 1,
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            text(L"Snip " + std::to_wstring(snip.sequence),
                 {cell.left + 8, cell.top + 80, cell.right - 60, cell.bottom},
                 current ? Accent : Ink, app.graphics.smallFont.get());
            wchar_t time[16]{};
            swprintf_s(time, L"%02u:%02u", snip.captured.wHour, snip.captured.wMinute);
            text(current ? L"Current" : time,
                 {cell.right - 60, cell.top + 80, cell.right - 6, cell.bottom},
                 current ? Accent : Muted, app.graphics.smallFont.get(), true);
        }
    }
    phase(app.paintTiming.content);
    HRESULT result;
    {
        ResizeTrace present("D2D.EndDraw");
        result = rt->EndDraw();
    }
    phase(app.paintTiming.present);
    if (app.resizeTest)
        ++app.resizeTestPaints;
    if (result == D2DERR_RECREATE_TARGET)
    {
        // The CPU preview is still valid; only target-owned GPU resources were lost.
        app.displayBitmap.reset();
        app.workspaceBrush.reset();
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
LRESULT CALLBACK textEditProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR,
                                   DWORD_PTR)
{
    return callbackBoundary<LRESULT>(
        [&]() -> LRESULT {
#ifdef TIGER_SNIP_TESTING
            if (testing::callbackCheckpoint)
                testing::callbackCheckpoint("textEditProcedure", message);
#endif
            try
            {
                if (message == WM_CHAR && wp == 2)
                    return 0; // Ctrl+B formats text; its translated control character is not
                              // content.
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
                        command(wp == 'B'                          ? TextBold
                                : wp == 'N'                        ? NewSnip
                                : (GetKeyState(VK_SHIFT) & 0x8000) ? SaveAs
                                                                   : Save);
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
        },
        [&](const char *failure) { error(app.window, failure); }, 0);
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
    HFONT font =
        CreateFontW(-std::max(1, static_cast<int>(std::round(item.fontSize * scale))), 0, 0, 0,
                    item.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
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
    const int contentWidth =
        static_cast<int>(std::ceil((item.b.x - item.a.x - padding * 2) * scale));
    const int width =
        std::min(availableWidth, std::max(24, std::max(contentWidth, int(measured.right)) + 4));
    const int height = std::min(
        availableHeight,
        std::max(
            24, std::max(int(measured.bottom),
                         static_cast<int>(std::ceil((item.b.y - item.a.y - padding * 2) * scale))) +
                    4));

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
        const bool unchanged =
            !app.textNew && item.text == app.textBefore.text &&
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
        item.textWidth =
            std::max(1.0f, std::min(600.0f, app.image.width - point.x - (item.boxed ? 24 : 0)));
        app.graphics.measureText(item);
        app.document.items.push_back(std::move(item));
        existing = static_cast<int>(app.document.items.size()) - 1;
    }
    app.document.selected = existing;
    app.textBefore = app.document.items[existing];
    app.tool = Tool::Text;
    app.textEdit = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0,
        0, 1, 1, app.window, reinterpret_cast<HMENU>(TextEditControl), app.instance, nullptr);
    if (!app.textEdit || !SetWindowSubclass(app.textEdit, textEditProcedure, 1, 0))
    {
        if (app.textEdit)
            DestroyWindow(std::exchange(app.textEdit, nullptr));
        app.document.cancel();
        throwWindowsError("Cannot open the inline text editor.");
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
    if (app.tool == Tool::Select && !selected())
        selectTool(Tool::Pen);
    // Recoloring a selected shape updates that shape tool's preference, even in Select mode.
    const size_t toolIndex =
        static_cast<size_t>(selected() ? app.document.items[app.document.selected].kind : app.tool);
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
void paletteChanged()
{
    app.paletteDirty = true;
    app.hover = 0;
    app.pressed = 0;
    buildButtons();
    updateView();
    syncTextEditor();
    repaint();
}
void addPaletteColor(Color value)
{
    if (std::find(app.palette.begin(), app.palette.end(), value) == app.palette.end())
    {
        if (app.palette.size() >= MaxPaletteColors)
            throw std::runtime_error("The palette is full. Delete a color before adding another.");
        app.palette.push_back(value);
        paletteChanged();
    }
    changeColor(value);
}
void editPaletteColor(size_t index, Color value)
{
    if (index >= app.palette.size() || app.palette[index] == value)
        return;
    const auto existing = std::find(app.palette.begin(), app.palette.end(), value);
    if (existing != app.palette.end())
        app.palette.erase(app.palette.begin() + index);
    else
        app.palette[index] = value;
    paletteChanged();
    changeColor(value);
}
void deletePaletteColor(size_t index)
{
    if (index >= app.palette.size())
        return;
    app.palette.erase(app.palette.begin() + index);
    paletteChanged();
}
void customColor(void (*test)(HWND) = nullptr)
{
    if (const auto value = pickPaletteColor(app.instance, app.window, activeColor(), false, test))
        addPaletteColor(*value);
    if (app.textEdit)
        SetFocus(app.textEdit);
}
void paletteMenu(POINT point)
{
    POINT client = point;
    ScreenToClient(app.window, &client);
    const auto button = std::find_if(app.buttons.begin(), app.buttons.end(), [&](const Button &b) {
        return paletteCommand(b.command) &&
               b.rect.contains({client.x / app.dpi, client.y / app.dpi});
    });
    if (button == app.buttons.end())
        return;
    const size_t index = button->command - ColorFirst;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Edit color...");
    AppendMenuW(menu, MF_STRING, 2, L"Delete color");
    const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                                      app.window, nullptr);
    DestroyMenu(menu);
    if (choice == 1)
    {
        if (const auto value = pickPaletteColor(app.instance, app.window, app.palette[index], true))
            editPaletteColor(index, *value);
    }
    else if (choice == 2)
        deletePaletteColor(index);
    if (app.textEdit)
        SetFocus(app.textEdit);
}
void changeThickness(int delta)
{
    if (textMode())
    {
        const bool current =
            selected() && app.document.items[app.document.selected].kind == Tool::Text;
        const auto *item = current ? &app.document.items[app.document.selected] : nullptr;
        changeTextFormatting((item ? item->fontSize : app.fontSize) + delta,
                             item ? item->bold : app.textBold, item ? item->boxed : app.textBox);
        return;
    }
    float previous = selected() && app.document.items[app.document.selected].kind != Tool::Check
                         ? app.document.items[app.document.selected].thickness
                         : brushWidth();
    const bool highlight = highlightMode();
    float value = std::clamp(previous + delta, highlight ? 4.0f : 1.0f, highlight ? 80.0f : 100.0f);
    float &rememberedWidth = highlight ? app.highlightWidth : app.thickness;
    if (rememberedWidth != value)
    {
        rememberedWidth = value;
        app.toolPreferencesDirty = true;
    }
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
    if (!SetTimer(app.window, SizeRepeatTimer, SizeRepeatInterval, nullptr))
    {
        stopSizeRepeat();
        return;
    }
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
    app.erasing = false;
    app.cropping = false;
    finishTextEditing();
    app.tool = tool;
    app.pickingColor = false;
    app.pickerImage = {};
    app.document.selected = -1;
    repaint();
}
void eraseBetween(Point from, Point to)
{
    // Clip the sweep to the visible screenshot, never to toolbar or padding pixels.
    auto r = canvasRect();
    const auto a = app.view.toScreen({0, 0});
    const auto b = app.view.toScreen(
        {static_cast<float>(app.image.width), static_cast<float>(app.image.height)});
    r = {std::max(r.left, a.x), std::max(r.top, a.y), std::min(r.right, b.x),
         std::min(r.bottom, b.y)};
    if (r.width() <= 0 || r.height() <= 0)
        return;
    const auto delta = to - from;
    float first = 0, last = 1;
    auto clip = [&](float p, float q) {
        if (p == 0)
            return q >= 0;
        const float t = q / p;
        if (p < 0)
            first = std::max(first, t);
        else
            last = std::min(last, t);
        return first <= last;
    };
    if (!clip(-delta.x, from.x - r.left) || !clip(delta.x, r.right - from.x) ||
        !clip(-delta.y, from.y - r.top) || !clip(delta.y, r.bottom - from.y))
        return;
    if (app.document.eraseAlong(app.view.toImage(from + delta * first),
                                app.view.toImage(from + delta * last), 6 / app.view.scale))
    {
        app.changed = true;
        repaint();
    }
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
void syncCroppedImage()
{
    if (app.cropSource.empty() || app.appliedCrop == app.document.cropBounds)
        return;
    if (app.document.cropBounds)
    {
        const auto r = *app.document.cropBounds;
        app.image = app.cropSource.crop(static_cast<int>(r.left), static_cast<int>(r.top),
                                        static_cast<int>(r.width()), static_cast<int>(r.height()));
    }
    else
        app.image = app.cropSource;
    app.appliedCrop = app.document.cropBounds;
    resetPreview();
    app.fit = true;
    updateView();
    repaint();
}
void applyCrop(Rect r)
{
    const int left = std::clamp(static_cast<int>(std::floor(r.left)), 0, app.image.width);
    const int top = std::clamp(static_cast<int>(std::floor(r.top)), 0, app.image.height);
    const int right = std::clamp(static_cast<int>(std::ceil(r.right)), 0, app.image.width);
    const int bottom = std::clamp(static_cast<int>(std::ceil(r.bottom)), 0, app.image.height);
    if (right - left < 2 || bottom - top < 2 ||
        (left == 0 && top == 0 && right == app.image.width && bottom == app.image.height))
        return;
    auto cropped = app.image.crop(left, top, right - left, bottom - top);
    app.document.begin();
    const auto previous = app.document.cropBounds.value_or(Rect{});
    app.document.cropBounds = Rect{previous.left + left, previous.top + top, previous.left + right,
                                   previous.top + bottom};
    for (auto &item : app.document.items)
        item.move({-static_cast<float>(left), -static_cast<float>(top)});
    app.document.selected = -1;
    app.document.commit();
    if (app.cropSource.empty())
        app.cropSource = std::move(app.image);
    app.image = std::move(cropped);
    app.appliedCrop = app.document.cropBounds;
    app.cropping = false;
    app.dirty = true;
    app.fit = true;
    resetPreview();
    updateView();
    repaint();
    updateTitle();
    status(L"Cropped to " + std::to_wstring(app.image.width) + L" x " +
           std::to_wstring(app.image.height) + L" - Ctrl+Z restores the image");
}
void mouseDown(LPARAM lp, bool middle = false)
{
    SetFocus(app.window);
    Point screen{GET_X_LPARAM(lp) / app.dpi, GET_Y_LPARAM(lp) / app.dpi};
    if (app.recentOpen)
    {
        if (recentPanelRect().contains(screen))
        {
            if (!middle)
                for (size_t i = 0; i < app.buttons.size(); ++i)
                {
                    const auto &button = app.buttons[i];
                    if (recentPanelCommand(button.command) && button.rect.contains(screen) &&
                        enabled(button.command))
                    {
                        app.pressed = static_cast<int>(i + 1);
                        app.hover = button.command;
                        SetCapture(app.window);
                        repaint();
                        break;
                    }
                }
            return; // Panel padding never draws on the screenshot beneath it.
        }
        const bool toggle =
            !middle &&
            std::any_of(app.buttons.begin(), app.buttons.end(), [&](const Button &button) {
                return button.command == RecentSnips && button.rect.contains(screen);
            });
        if (!toggle)
        {
            closeRecent();
            buildButtons();
            return; // Clicking away dismisses the popup without starting an annotation.
        }
    }
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
                        if (!SetTimer(app.window, SizeRepeatTimer, SizeRepeatDelay, nullptr))
                            app.sizeRepeatCommand = 0; // A single click remains available.
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
    if (middle || app.spaceDown || handPanAt(screen))
    {
        updateView();
        if (!canPanImage())
            return;
        finishTextEditing();
        app.drag = Drag::Pan;
        app.dragStart = screen;
        app.panStart = app.view.origin;
        app.fit = false;
        SetCapture(app.window);
        refreshEditorCursor();
        return;
    }
    Point p = app.view.toImage(screen);
    if (app.cropping)
    {
        if (!app.image.sample(p))
            return;
        app.dragStart = app.cropEnd = limited(p);
        app.drag = Drag::Crop;
        SetCapture(app.window);
        repaint();
        return;
    }
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
    if (app.erasing)
    {
        app.document.begin();
        app.document.selected = -1;
        app.changed = false;
        app.drag = Drag::Erase;
        app.dragStart = screen;
        SetCapture(app.window);
        eraseBetween(screen, screen);
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
    item.thickness = brushWidth();
    item.style = app.styles[static_cast<size_t>(app.tool)];
    item.a = item.b = p;
    if (app.tool == Tool::Pen || app.tool == Tool::Highlight)
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
                if (app.recentOpen && recentPanelRect().contains(screen) &&
                    !recentPanelCommand(button.command))
                    continue;
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
        app.view.origin = app.panStart + delta;
        updateView();
        repaint();
        return;
    }
    if (app.drag == Drag::Crop)
    {
        app.cropEnd = limited(app.view.toImage(screen));
        repaint();
        return;
    }
    if (app.drag == Drag::Erase)
    {
        if (length(screen - app.dragStart) >= .25f)
        {
            eraseBetween(app.dragStart, screen);
            app.dragStart = screen;
        }
        return;
    }
    if (!selected())
        return;
    Point p = app.view.toImage(screen);
    auto &item = app.document.items[app.document.selected];
    if (app.drag == Drag::Draw)
    {
        p = limited(p);
        if (app.tool == Tool::Pen || app.tool == Tool::Highlight)
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
        else if ((app.tool == Tool::Circle || app.tool == Tool::Rectangle) &&
                 (GetKeyState(VK_SHIFT) & 0x8000))
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
            if (item.kind == Tool::Check || item.kind == Tool::Text ||
                (GetKeyState(VK_SHIFT) & 0x8000))
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
    if (app.drag == Drag::Erase)
        mouseMove(lp);
    if (app.drag == Drag::Crop)
    {
        mouseMove(lp);
        const auto crop = rectangle(app.dragStart, app.cropEnd);
        finishDrag();
        applyCrop(crop);
        return;
    }
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
        if (item.kind != Tool::Pen && item.kind != Tool::Highlight && length(item.b - item.a) < 4)
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
        if (item.kind != Tool::Pen && item.kind != Tool::Highlight)
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
    finishTextEditing();
    finishDrag(true);
    updateView();
    const auto r = navigationRect(), content = imageContentRect();
    const float minimum = View::fittedScale(r, content, 1 / app.dpi);
    const float next = std::clamp(app.view.scale * factor, minimum, 8 / app.dpi);
    app.fit = next <= minimum + .00001f;
    app.view.zoomAt(screen, next, r, content);
    updateView();
    repaint();
    refreshEditorCursor();
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
    if (!SetTimer(app.window, CopyFlashTimer, 16, nullptr))
        app.copyFlashStarted = false; // Copy already succeeded; omit the optional animation.
    repaint();
}
void copyImage(bool automatic = false)
{
    if (!hasImage())
        return;
    const auto &bitmap = previewImage();
    auto png = app.graphics.png(bitmap);
    ClipboardFailure failure;
    if (!copyBitmap(app.window, bitmap, png, &failure))
    {
        if (failure.unavailable)
            status(L"Clipboard is busy. Try Ctrl+C again.");
        else if (automatic)
            status(L"Could not auto copy. Press Ctrl+C to retry.");
        else
            error(app.window, windowsError(failure.operation, failure.code).c_str());
        return;
    }
    app.dirty = false;
    updateTitle();
    if (!automatic)
        startCopyFeedback();
    status(automatic ? L"Copied automatically - ready to paste; Ctrl+C copies your edits"
                     : L"Copied image and annotations - ready to paste");
}
bool existingFolder(const std::wstring &path)
{
    const DWORD attributes =
        path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring initialSavePath(const std::wstring &path)
{
    if (!existingFolder(app.saveFolder))
        return path;
    // A full filename makes the explicit preference win over the dialog's recent folder.
    return (std::filesystem::path(app.saveFolder) / std::filesystem::path(path).filename())
        .wstring();
}
void setSaveFolder(const std::wstring &folder)
{
    if (!existingFolder(folder))
        throw std::runtime_error("Choose an existing folder for saved snips.");
    commitPreferences(app.iniPath, {{L"Settings", L"SaveFolder", folder}});
    app.saveFolder = folder;
}
void chooseSaveFolder()
{
    Com<IFileOpenDialog> dialog;
    check(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                           __uuidof(IFileOpenDialog), reinterpret_cast<void **>(dialog.put())),
          "Cannot open the folder picker.");
    DWORD options = 0;
    check(dialog->GetOptions(&options), "Cannot read folder picker options.");
    check(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                             FOS_NOCHANGEDIR),
          "Cannot configure the folder picker.");
    check(dialog->SetTitle(L"Save location for snips"), "Cannot set the folder picker title.");
    check(dialog->SetOkButtonLabel(L"Use this folder"), "Cannot label the folder picker.");
    if (existingFolder(app.saveFolder))
    {
        Com<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(app.saveFolder.c_str(), nullptr,
                                                  __uuidof(IShellItem),
                                                  reinterpret_cast<void **>(folder.put()))))
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
        const DWORD failure = CommDlgExtendedError();
        if (failure)
            throwWindowsError("Windows could not open the save dialog.", failure);
        return false;
    }
    path = buffer;
    auto dot = path.find_last_of(L'.'), slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        path += L".png";
    else if (_wcsicmp(path.substr(dot).c_str(), L".png") != 0)
    {
        MessageBoxW(IsWindowVisible(app.window) ? app.window : nullptr,
                    L"Tiger Snip saves PNG images. Use a filename ending in .png.", L"Save as PNG",
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
        app.instantHotkeyControl = nullptr;
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
    int width = static_cast<int>(460 * d), height = static_cast<int>(330 * d);
    app.settingsWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, SettingsClass, L"Keyboard shortcuts",
                                         WS_CAPTION | WS_SYSMENU,
                                         owner.left + (owner.right - owner.left - width) / 2,
                                         owner.top + (owner.bottom - owner.top - height) / 2, width,
                                         height, app.window, nullptr, app.instance, nullptr);
    if (!app.settingsWindow)
        throwWindowsError("Cannot open shortcut settings.");
    EnableWindow(app.window, FALSE);
    ShowWindow(app.settingsWindow, SW_SHOW);
    SetFocus(app.hotkeyControl);
}
void trayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, NewSnip, L"Snip now");
    AppendMenuW(menu, MF_STRING, InstantSnip, L"Capture all monitors now");
    AppendMenuW(menu, MF_STRING, ShowEditor, L"Open editor");
    AppendMenuW(menu, MF_STRING, Settings, L"Keyboard shortcuts...");
    AppendMenuW(menu, MF_STRING | (app.autoCopy ? MF_CHECKED : 0), AutoCopy,
                L"Auto copy new snips");
    AppendMenuW(menu, MF_STRING, SaveLocation, L"Save location...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Exit, L"Exit Tiger Snip");
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
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), app.dpi * 96,
        app.dpi * 96);
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
    brush->SetColor(color(hover    ? rgb(233, 226, 255)
                          : chosen ? rgb(246, 242, 255)
                                   : rgb(255, 255, 255)));
    target->FillRoundedRectangle(D2D1::RoundedRect({3, 2, width - 3, height - 2}, 7, 7),
                                 brush.get());
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
    target->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale) *
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
        choices = {{Tool::Circle, 0, L"Circle"},
                   {Tool::Circle, 1, L"Highlight circle"},
                   {Tool::Circle, 2, L"Dashed circle"},
                   {Tool::Rectangle, 0, L"Square"},
                   {Tool::Rectangle, 1, L"Rounded square"},
                   {Tool::Rectangle, 2, L"Highlight box"},
                   {Tool::Rectangle, 3, L"Filled box"}};
    else if (id == ArrowStyleMenu)
        choices = {{Tool::Arrow, 0, L"Classic"},
                   {Tool::Arrow, 1, L"Outlined"},
                   {Tool::Arrow, 2, L"Curved gloss"},
                   {Tool::Arrow, 3, L"Straight gloss"},
                   {Tool::Arrow, 4, L"Block gloss"}};
    else if (id == CheckStyleMenu)
        choices = {{Tool::Check, 0, L"Boxed check"},    {Tool::Check, 1, L"Circle badge"},
                   {Tool::Check, 2, L"Simple check"},   {Tool::Check, 3, L"Boxed X"},
                   {Tool::Check, 4, L"Circle X badge"}, {Tool::Check, 5, L"Simple X"}};
    else
        choices = {
            {Tool::Line, 0, L"Solid"}, {Tool::Line, 1, L"Dashed"}, {Tool::Line, 2, L"Dotted"}};
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
    const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.x, anchor.y, 0,
                                      app.window, nullptr);
    app.shapeMenu = nullptr;
    DestroyMenu(menu);
    if (choice)
        command(choice);
}
void command(int id)
{
    if (id != RecentSnips && !recentPanelCommand(id))
        closeRecent();
    if (app.sizeRepeatCommand && id != app.sizeRepeatCommand)
    {
        stopSizeRepeat();
        app.pressed = 0;
        if (GetCapture() == app.window)
            ReleaseCapture();
    }
    const bool textFormatting =
        id == TextBold || id == TextBox || id == TextSizeMenu || id == SizeDown || id == SizeUp ||
        id == CustomColor || id == Eyedropper || paletteCommand(id) ||
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
    if (recentChoice(id))
    {
        if (enabled(id))
            restoreRecentSnip(id - RecentChoiceFirst);
        return;
    }
    if (id == RecentSnips)
    {
        if (!enabled(id))
            return;
        if (app.recentOpen)
            closeRecent();
        else
        {
            app.cropping = false;
            app.spaceDown = false;
            refreshRecentThumbnail();
            app.recentFocus = app.activeRecent >= 0
                                  ? static_cast<int>(app.recent.size()) - 1 - app.activeRecent
                                  : 0;
            app.recentScroll = std::max(0, app.recentFocus / 2 - recentVisibleRows() + 1);
            app.recentOpen = true;
        }
        buildButtons();
        repaint();
        return;
    }
    if (id == RecentClose || id == RecentNewer || id == RecentOlder)
    {
        if (!enabled(id))
            return;
        if (id == RecentClose)
            closeRecent();
        else
        {
            app.recentScroll += id == RecentOlder ? 1 : -1;
            app.recentFocus = app.recentScroll * 2;
        }
        buildButtons();
        repaint();
        return;
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
    if (paletteCommand(id))
    {
        changeColor(app.palette[id - ColorFirst]);
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
        const Tool tool =
            static_cast<Tool>(static_cast<int>(Tool::Circle) + option / StyleChoiceStride);
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
        changeTextFormatting(static_cast<float>(FontSizes[id - TextSizeFirst]), active(TextBold),
                             active(TextBox));
        return;
    }
    switch (id)
    {
    case CropTool:
        if (hasImage())
        {
            app.erasing = false;
            app.cropping = !app.cropping;
            app.document.selected = -1;
            app.status.clear();
            refreshEditorCursor();
            repaint();
        }
        break;
    case EraserTool:
        if (hasImage())
        {
            const bool wasErasing = app.erasing;
            selectTool(Tool::Select);
            app.erasing = !wasErasing;
            app.status.clear();
            repaint();
        }
        break;
    case InstantSnip:
        startSnip(true, true);
        break;
    case HighlightTool:
        if (hasImage())
            selectTool(Tool::Highlight);
        break;
    case TextTool:
        if (hasImage())
            selectTool(Tool::Text);
        break;
    case RectangleTool:
        if (hasImage())
            command(
                styleCommand(Tool::Rectangle, app.styles[static_cast<size_t>(Tool::Rectangle)]));
        break;
    case TextBold:
    case TextBox: {
        const float size =
            selected() && app.document.items[app.document.selected].kind == Tool::Text
                ? app.document.items[app.document.selected].fontSize
                : app.fontSize;
        changeTextFormatting(size, id == TextBold ? !active(TextBold) : active(TextBold),
                             id == TextBox ? !active(TextBox) : active(TextBox));
        break;
    }
    case TextSizeMenu: {
        HMENU menu = CreatePopupMenu();
        if (!menu)
            break;
        const float size =
            selected() && app.document.items[app.document.selected].kind == Tool::Text
                ? app.document.items[app.document.selected].fontSize
                : app.fontSize;
        for (size_t i = 0; i < FontSizes.size(); ++i)
            AppendMenuW(menu, MF_STRING | (FontSizes[i] == size ? MF_CHECKED : 0),
                        TextSizeFirst + i, (std::to_wstring(FontSizes[i]) + L" px").c_str());
        POINT point{};
        GetCursorPos(&point);
        const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y,
                                          0, app.window, nullptr);
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
    case AutoCopy:
        commitPreferences(app.iniPath, {{L"Settings", L"AutoCopy", app.autoCopy ? L"0" : L"1"}});
        app.autoCopy = !app.autoCopy;
        status(app.autoCopy ? L"Auto copy enabled for new snips"
                            : L"Auto copy disabled - use Copy or Ctrl+C");
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
            syncCroppedImage();
            app.dirty = true;
            updateTitle();
            repaint();
        }
        break;
    case Redo:
        if (app.document.redo())
        {
            syncCroppedImage();
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
    case FlipCurvedArrow:
        if (enabled(id))
        {
            app.document.begin();
            app.document.items[app.document.selected].flipCurvedArrow();
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
    case FullScreen:
        toggleFullScreen();
        break;
    case ToggleActions:
    case ToggleTools:
    case ToggleFormatting: {
        app.collapsedRows ^= 1U << (id - ToggleActions);
        app.layoutPreferencesDirty = true;

        updateView();
        buildButtons();
        repaint();
        break;
    }
    case Actual:
        if (hasImage())
        {
            updateView();
            const auto r = navigationRect();
            app.fit = false;
            app.view.scale = 1 / app.dpi;
            app.view.origin =
                Point{(r.left + r.right) / 2, (r.top + r.bottom) / 2} -
                Point{app.image.width / 2.0f, app.image.height / 2.0f} * app.view.scale;
            updateView();
            repaint();
        }
        break;
    case CustomColor:
        customColor();
        break;
    case Eyedropper:
        if (hasImage())
        {
            app.erasing = false;
            if (app.pickingColor)
            {
                app.pickingColor = false;
                app.pickerImage = {};
            }
            else
            {
                app.pickerImage =
                    app.graphics.exportImage(app.image, app.document.items, app.exportOptions);
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
    case RenderingSettings:
        openRenderingSettings();
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
        MessageBoxW(app.window,
                    L"Tiger Snip 1.0.2\n\nNative C++ screenshot editor.\nDeveloped by Jack "
                    L"Kempf\n\nCtrl+N: new snip\nCtrl+C: "
                    L"copy image with annotations\nCtrl+S: save PNG\nCtrl+Shift+S: Save As\nCtrl+Z "
                    L"/ Ctrl+Y: undo / redo\nV / P / H / T / O / A / K / L: select / pen / "
                    L"highlight / text / circle / arrow / "
                    L"check / line\n[ / ]: brush size\nDelete: remove selection\nMouse wheel: "
                    L"zoom from Fit to 800%\nSelect + drag image: pan (also middle-drag or "
                    L"Space+drag)\nEsc: cancel capture or current "
                    L"edit\n\nClose the window to stay in the tray.\nFile > Exit quits "
                    L"completely.\n\nShortcut settings are saved in your personal Tiger Snip "
                    L"settings folder.",
                    L"About Tiger Snip", MB_OK | MB_ICONINFORMATION);
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
void freezeDesktop(bool includeCursor = false)
{
    app.virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    app.virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN),
              height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    DwmFlush();
    app.desktop = captureDesktop(app.virtualX, app.virtualY, width, height, includeCursor);
}
void acceptCapture(Bitmap captured)
{
    cancelCapture(false);
    stashRecentSnip();
    releaseImage();
    if (app.recent.size() == RecentLimit)
        app.recent.erase(app.recent.begin());
    RecentSnip entry;
    GetLocalTime(&entry.captured);
    entry.sequence = ++app.recentSequence;
    app.recent.push_back(std::move(entry));
    app.activeRecent = static_cast<int>(app.recent.size()) - 1;
    app.image = std::move(captured);
    app.tool = Tool::Pen;
    app.erasing = false;
    app.fit = true;
    app.status.clear();
    updateTitle();
    showEditor();
    // Diagnostic capture suites must leave the user's clipboard untouched.
    if (app.autoCopy && !app.smoke && !app.resizeTest)
    {
        try
        {
            copyImage(true);
        }
        catch (const std::exception &)
        {
            status(L"Could not auto copy. Press Ctrl+C to retry.");
        }
    }
}
void startSnip(bool instant, bool allMonitors)
{
    if (app.overlay || app.capturePending || app.settingsWindow)
        return;
    closeRecent();
    // Reserve the request so repeated snips cannot replace an in-progress capture.
    app.capturePending = true;
    instant = instant || allMonitors;
    try
    {
        if (instant)
        {
            cloakEditorForCapture();
            freezeDesktop(allMonitors);
        }
        finishDrag(true);
        finishTextEditing();
        hideEditorForCapture();
        if (allMonitors)
        {
            acceptCapture(std::move(app.desktop));
            if (!app.autoCopy || app.smoke || app.resizeTest)
                status(L"Captured all monitors with the pointer - Crop (C) keeps just the area you "
                       L"need");
        }
        else if (instant)
            openOverlay();
        else if (!SetTimer(app.window, CaptureTimer, 65, nullptr))
            throwWindowsError("Cannot start screen capture.");
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
                                  L"Tiger Snip selection", WS_POPUP, app.virtualX, app.virtualY,
                                  width, height, nullptr, nullptr, app.instance, nullptr);
    if (!app.overlay)
        throwWindowsError("Cannot open the selection overlay.");
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
    acceptCapture(std::move(captured));
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
    AppendMenuW(file, MF_STRING, InstantSnip, L"Capture &all monitors now");
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
    AppendMenuW(edit, MF_STRING, CropTool, L"&Crop image\tC");
    AppendMenuW(edit, MF_STRING, EraserTool, L"&Eraser\tE");
    AppendMenuW(view, MF_STRING, Fit, L"&Fit image");
    AppendMenuW(view, MF_STRING, Actual, L"&Actual size (100%)");
    AppendMenuW(view, MF_STRING, FullScreen, L"&Full screen\tF11");
    AppendMenuW(view, MF_STRING, RecentSnips, L"&Recent snips\tCtrl+Shift+R");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ToggleActions, L"&Actions row");
    AppendMenuW(view, MF_STRING, ToggleTools, L"&Tools and shapes row");
    AppendMenuW(view, MF_STRING, ToggleFormatting, L"Color and &size row");
    AppendMenuW(settings, MF_STRING, Settings, L"&Keyboard shortcuts...");
    AppendMenuW(settings, MF_STRING, AutoCopy, L"Auto &copy new snips");
    AppendMenuW(settings, MF_STRING, RenderingSettings, L"&Rendering...");
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
    CheckMenuItem(menu, AutoCopy, MF_BYCOMMAND | (app.autoCopy ? MF_CHECKED : MF_UNCHECKED));
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
    for (int id : {Copy, Save, SaveAs, Fit, Actual, CropTool, EraserTool})
        enable(id, hasImage());
    CheckMenuItem(menu, EraserTool, MF_BYCOMMAND | (app.erasing ? MF_CHECKED : MF_UNCHECKED));
    enable(Undo, app.document.canUndo());
    enable(Redo, app.document.canRedo());
    enable(DeleteSelected, selected());
    enable(RecentSnips, enabled(RecentSnips));
    enable(Clear, !app.document.items.empty());
    CheckMenuItem(menu, Startup, MF_BYCOMMAND | (startupEnabled() ? MF_CHECKED : MF_UNCHECKED));
}
void processKey(WPARAM key)
{
    if (app.recentOpen)
    {
        if (key == VK_ESCAPE)
        {
            closeRecent();
            buildButtons();
            return;
        }
        if (key == VK_RETURN || key == VK_SPACE)
        {
            restoreRecentSnip(static_cast<int>(app.recent.size()) - 1 - app.recentFocus);
            return;
        }
        int next = app.recentFocus;
        if (key == VK_LEFT)
            --next;
        else if (key == VK_RIGHT)
            ++next;
        else if (key == VK_UP)
            next -= 2;
        else if (key == VK_DOWN)
            next += 2;
        else if (key == VK_HOME)
            next = 0;
        else if (key == VK_END)
            next = static_cast<int>(app.recent.size()) - 1;
        else if (key == VK_PRIOR)
            next -= recentVisibleRows() * 2;
        else if (key == VK_NEXT)
            next += recentVisibleRows() * 2;
        else if (!(GetKeyState(VK_CONTROL) & 0x8000))
            return;
        app.recentFocus = std::clamp(next, 0, static_cast<int>(app.recent.size()) - 1);
        if (app.recentFocus / 2 < app.recentScroll)
            app.recentScroll = app.recentFocus / 2;
        if (app.recentFocus / 2 >= app.recentScroll + recentVisibleRows())
            app.recentScroll = app.recentFocus / 2 - recentVisibleRows() + 1;
        buildButtons();
        repaint();
        if (key >= VK_PRIOR && key <= VK_DOWN)
            return;
        if (key == VK_HOME || key == VK_END)
            return;
    }
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
        else if (app.cropping)
        {
            finishDrag(true);
            app.cropping = false;
            app.status.clear();
            refreshEditorCursor();
            repaint();
        }
        else if (app.drag != Drag::None)
            finishDrag(true);
        else if (app.fullScreen)
            command(FullScreen);
        else
        {
            app.erasing = false;
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
        case 'R':
            if (shift)
                command(RecentSnips);
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
    case 'C':
        command(CropTool);
        break;
    case 'E':
        command(EraserTool);
        break;
    case VK_DELETE:
        command(DeleteSelected);
        break;
    case 'V':
        command(SelectTool);
        break;
    case 'P':
        command(PenTool);
        break;
    case 'H':
        command(HighlightTool);
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
LRESULT mainMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == app.taskbarCreated)
    {
        app.tray = false;
        addTray();
        if (!app.tray)
            showEditor(); // Remain accessible if Explorer cannot restore the icon.
        return 0;
    }
    switch (message)
    {
    case WM_CREATE:
        app.window = hwnd;
        chooseWelcomeMessage();
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
        finishTextEditing();
        if (wp != SIZE_MINIMIZED)
        {
            repaint();
            // Maximize/restore must replace the old frame before returning to Windows.
            // During border dragging, let the modal sizing loop coalesce paint requests.
            if (!app.interactiveResize && IsWindowVisible(hwnd))
                UpdateWindow(hwnd);
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        app.interactiveResize = true;
        return 0;
    case WM_EXITSIZEMOVE:
        app.interactiveResize = false;
        repaint();
        UpdateWindow(hwnd);
        return 0;
    case WM_DPICHANGED: {
        const float previousDpi = app.dpi;
        const auto r = app.viewViewport;
        const Point center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
        const auto focus = app.view.toImage(center);
        app.dpi = HIWORD(wp) / 96.0f;
        app.view.scale *= previousDpi / app.dpi;
        app.view.origin = center - focus * app.view.scale;
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
    case WM_MEASUREITEM: {
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
    case WM_DRAWITEM: {
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
    case WM_CONTEXTMENU:
        if (GET_X_LPARAM(lp) != -1 || GET_Y_LPARAM(lp) != -1)
            paletteMenu({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;
    case WM_INITMENUPOPUP:
        updateMenus();
        return 0;
    case WM_LBUTTONDOWN:
        mouseDown(lp);
        return 0;
    case WM_LBUTTONDBLCLK: {
        if (app.recentOpen)
        {
            mouseDown(lp);
            return 0;
        }
        Point screen{GET_X_LPARAM(lp) / app.dpi, GET_Y_LPARAM(lp) / app.dpi};
        const bool onButton =
            std::any_of(app.buttons.begin(), app.buttons.end(),
                        [&](const Button &button) { return button.rect.contains(screen); });
        if (!onButton && hasImage() && !app.erasing && canvasRect().contains(screen))
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
        closeRecent();
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
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (app.recentOpen)
        {
            if (recentPanelRect().contains({p.x / app.dpi, p.y / app.dpi}))
            {
                if (delta)
                    command(delta < 0 ? RecentOlder : RecentNewer);
            }
            else
                closeRecent();
            return 0;
        }
        if (!hasImage())
            return 0;
        if (delta && canvasRect().contains({p.x / app.dpi, p.y / app.dpi}))
            zoomAt({p.x / app.dpi, p.y / app.dpi}, std::pow(1.2f, delta / 120.0f));
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
        {
            if (wp == static_cast<WPARAM>(app.hotkeyId))
                startSnip(true);
            else if (wp == static_cast<WPARAM>(app.instantHotkeyId))
                startSnip(true, true);
        }
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
        finishTextEditing();
        saveToolPreferencesOrNotify();
        if (app.tray)
        {
            stashRecentSnip();
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
        app.recent.clear();
        app.activeRecent = -1;
        app.recentOpen = false;
        stopSizeRepeat();
        finishTextEditing(true);
        for (auto &cursor : app.grabCursor)
            if (cursor)
            {
                if (GetCursor() == cursor)
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                DestroyCursor(cursor);
                cursor = nullptr;
            }
        if (app.penCursor)
        {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            DestroyCursor(app.penCursor);
            app.penCursor = nullptr;
        }
        if (app.eraserCursor)
        {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            DestroyCursor(app.eraserCursor);
            app.eraserCursor = nullptr;
        }
        saveToolPreferences();
        cancelCapture(false);
        removeTray();
        if (app.hotkeyRegistered)
            UnregisterHotKey(hwnd, app.hotkeyId);
        if (app.instantHotkeyRegistered)
            UnregisterHotKey(hwnd, app.instantHotkeyId);
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
    return callbackBoundary<LRESULT>(
        [&]() -> LRESULT {
#ifdef TIGER_SNIP_TESTING
            if (testing::callbackCheckpoint)
                testing::callbackCheckpoint("mainProcedure", message);
#endif
            const char *operation = message == WM_SIZE    ? "WM_SIZE"
                                    : message == WM_PAINT ? "WM_PAINT"
                                    : message == WM_SYSCOMMAND
                                        ? ((wp & 0xfff0) == SC_MAXIMIZE  ? "SC_MAXIMIZE"
                                           : (wp & 0xfff0) == SC_RESTORE ? "SC_RESTORE"
                                                                         : "WM_SYSCOMMAND")
                                    : message == WM_NCLBUTTONDBLCLK   ? "WM_NCLBUTTONDBLCLK"
                                    : message == WM_WINDOWPOSCHANGING ? "WM_WINDOWPOSCHANGING"
                                    : message == WM_WINDOWPOSCHANGED  ? "WM_WINDOWPOSCHANGED"
                                    : message == WM_NCCALCSIZE        ? "WM_NCCALCSIZE"
                                    : message == WM_NCPAINT           ? "WM_NCPAINT"
                                    : message == WM_DPICHANGED        ? "WM_DPICHANGED"
                                    : message == WM_TIMER && wp == TraceHeartbeatTimer ? "heartbeat"
                                                                                       : nullptr;
            // Trace resize messages, diagnostic heartbeats, and other messages taking over 100 ms.
            // Normal launches perform no diagnostic I/O or heartbeat polling.
            ResizeTrace trace(operation, message);
            return mainMessage(hwnd, message, wp, lp);
        },
        [&](const char *failure) {
            if (message == WM_PAINT)
            {
                ValidateRect(hwnd, nullptr);
                resetPreview();
                app.target.reset();
            }
            error(hwnd, failure);
        },
        message == WM_CREATE ? -1 : 0);
}
LRESULT CALLBACK overlayProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    return callbackBoundary<LRESULT>(
        [&]() -> LRESULT {
#ifdef TIGER_SNIP_TESTING
            if (testing::callbackCheckpoint)
                testing::callbackCheckpoint("overlayProcedure", message);
#endif
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
        },
        [&](const char *failure) {
            error(app.window, failure);
            cancelCapture();
        },
        0);
}
LRESULT CALLBACK shortcutFieldProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR,
                                        DWORD_PTR value)
{
    return callbackBoundary<LRESULT>(
        [&]() -> LRESULT {
#ifdef TIGER_SNIP_TESTING
            if (testing::callbackCheckpoint)
                testing::callbackCheckpoint("shortcutFieldProcedure", message);
#endif
            // The native hotkey control can store VK_PAUSE yet paint its name as blank.
            // Use an edit control for the label and retain the WORD format of existing settings.
            if (message == HKM_GETHOTKEY)
                return static_cast<WORD>(value);
            if (message == HKM_SETHOTKEY)
            {
                const auto key = static_cast<WORD>(wp);
                if (!SetWindowSubclass(hwnd, shortcutFieldProcedure, 1, key))
                    throwWindowsError("Cannot update the shortcut control.");
                SetWindowTextW(hwnd, hotkeyName(key).c_str());
                SendMessageW(hwnd, EM_SETSEL, -1, -1);
                return 0;
            }
            if (message == HKM_SETRULES)
                return 0;
            if (message == WM_GETDLGCODE)
                return DefSubclassProc(hwnd, message, wp, lp) | DLGC_WANTALLKEYS;
            if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            {
                if (wp == VK_TAB)
                {
                    SetFocus(
                        GetNextDlgTabItem(GetParent(hwnd), hwnd, GetKeyState(VK_SHIFT) & 0x8000));
                    return 0;
                }
                if (wp == VK_RETURN || wp == VK_ESCAPE)
                {
                    SendMessageW(GetParent(hwnd), WM_COMMAND, wp == VK_RETURN ? IDOK : IDCANCEL, 0);
                    return 0;
                }
                if (wp == VK_BACK || wp == VK_DELETE)
                {
                    SendMessageW(hwnd, HKM_SETHOTKEY, 0, 0);
                    return 0;
                }
                if (wp == VK_CONTROL || wp == VK_MENU || wp == VK_SHIFT || wp == VK_LWIN ||
                    wp == VK_RWIN)
                    return 0;
                BYTE modifiers = 0;
                if (GetKeyState(VK_CONTROL) & 0x8000)
                    modifiers |= HOTKEYF_CONTROL;
                if (GetKeyState(VK_MENU) & 0x8000)
                    modifiers |= HOTKEYF_ALT;
                if (GetKeyState(VK_SHIFT) & 0x8000)
                    modifiers |= HOTKEYF_SHIFT;
                if (lp & (1LL << 24))
                    modifiers |= HOTKEYF_EXT;
                SendMessageW(hwnd, HKM_SETHOTKEY, MAKEWORD(static_cast<BYTE>(wp), modifiers), 0);
                return 0;
            }
            if (message == WM_CHAR || message == WM_SYSCHAR)
                return 0;
            if (message == WM_NCDESTROY)
                RemoveWindowSubclass(hwnd, shortcutFieldProcedure, 1);
            return DefSubclassProc(hwnd, message, wp, lp);
        },
        [&](const char *failure) { error(GetParent(hwnd), failure); }, 0);
}
LRESULT CALLBACK settingsProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    return callbackBoundary<LRESULT>(
        [&]() -> LRESULT {
#ifdef TIGER_SNIP_TESTING
            if (testing::callbackCheckpoint)
                testing::callbackCheckpoint("settingsProcedure", message);
#endif
            switch (message)
            {
            case WM_CREATE: {
                float d = dpiFor(hwnd);
                auto control = [&](const wchar_t *cls, const wchar_t *label, DWORD style, int x,
                                   int y, int w, int h, int id) {
                    HWND item = CreateWindowExW(
                        0, cls, label, WS_CHILD | WS_VISIBLE | style, static_cast<int>(x * d),
                        static_cast<int>(y * d), static_cast<int>(w * d), static_cast<int>(h * d),
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app.instance,
                        nullptr);
                    if (!item)
                        throwWindowsError("Cannot create a settings control.");
                    SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(app.dialogFont), TRUE);
                    return item;
                };
                control(L"STATIC", L"Select an area (click and drag):", 0, 20, 15, 395, 24, 0);
                app.hotkeyControl =
                    control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, 20,
                            48, 395, 30, 10);
                if (!SetWindowSubclass(app.hotkeyControl, shortcutFieldProcedure, 1, 0))
                    throwWindowsError("Cannot initialize the area shortcut control.");
                SendMessageW(app.hotkeyControl, HKM_SETRULES, 0, 0);
                SendMessageW(app.hotkeyControl, HKM_SETHOTKEY, app.hotkey, 0);
                control(L"STATIC", L"Instant capture (all monitors and mouse pointer):", 0, 20, 91,
                        395, 24, 0);
                app.instantHotkeyControl =
                    control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, 20,
                            124, 395, 30, 11);
                if (!SetWindowSubclass(app.instantHotkeyControl, shortcutFieldProcedure, 1, 0))
                    throwWindowsError("Cannot initialize the instant shortcut control.");
                SendMessageW(app.instantHotkeyControl, HKM_SETRULES, 0, 0);
                SendMessageW(app.instantHotkeyControl, HKM_SETHOTKEY, app.instantHotkey, 0);
                control(L"STATIC",
                        L"Use Ctrl or Alt, or Pause alone. Backspace disables.\nInstant capture "
                        L"opens directly in the editor; use Crop (C).",
                        0, 20, 170, 395, 44, 0);
                control(L"BUTTON", L"Save", WS_TABSTOP | BS_DEFPUSHBUTTON, 237, 246, 84, 30, IDOK);
                control(L"BUTTON", L"Cancel", WS_TABSTOP, 331, 246, 84, 30, IDCANCEL);
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
                    WORD value =
                        static_cast<WORD>(SendMessageW(app.hotkeyControl, HKM_GETHOTKEY, 0, 0));
                    WORD full = static_cast<WORD>(
                        SendMessageW(app.instantHotkeyControl, HKM_GETHOTKEY, 0, 0));
                    if (!registerShortcuts(value, full))
                        return 0;
                    app.shortcutsDirty = true;
                    bool saved = saveToolPreferences();
                    closeSettings();
                    status(L"Area: " + hotkeyName(app.hotkey) + L" | All monitors: " +
                           hotkeyName(app.instantHotkey));
                    if (!saved)
                        error(app.window, app.preferenceError.c_str());
                    return 0;
                }
                break;
            case WM_CTLCOLORSTATIC:
                if (reinterpret_cast<HWND>(lp) == app.hotkeyControl ||
                    reinterpret_cast<HWND>(lp) == app.instantHotkeyControl)
                {
                    HDC dc = reinterpret_cast<HDC>(wp);
                    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
                    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
                }
                break;
            case WM_CLOSE:
                closeSettings();
                return 0;
            default:
                break;
            }
            return DefWindowProcW(hwnd, message, wp, lp);
        },
        [&](const char *failure) {
            error(app.window, failure);
            if (message == WM_CREATE)
            {
                app.hotkeyControl = app.instantHotkeyControl = nullptr;
                EnableWindow(app.window, TRUE);
            }
            else
                closeSettings();
        },
        message == WM_CREATE ? -1 : 0);
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
    cls.lpszClassName = app.diagnosticInstance ? DiagnosticClass : MainClass;
    cls.lpfnWndProc = mainProcedure;
    if (!RegisterClassExW(&cls))
        throwWindowsError("Cannot register the editor window.");
    cls.style = 0;
    cls.lpszClassName = OverlayClass;
    cls.lpfnWndProc = overlayProcedure;
    cls.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    if (!RegisterClassExW(&cls))
        throwWindowsError("Cannot register capture window.");
    cls.lpszClassName = SettingsClass;
    cls.lpfnWndProc = settingsProcedure;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&cls))
        throwWindowsError("Cannot register settings window.");
}
std::filesystem::path testReportDirectory;
void writeTestReport(const wchar_t *filename, const std::string &content)
{
    checkedTestReport(testReportDirectory / filename, content);
}
void testPenSizePreferences()
{
    WritePrivateProfileStringW(L"ToolPreferences", L"StrokeWidth", nullptr, app.iniPath.c_str());
    app.thickness = 19;
    loadToolPreferences();
    if (app.thickness != 4 || app.toolPreferencesDirty)
        throw std::runtime_error("Missing stroke-width setting did not restore the default.");
    app.tool = Tool::Pen;
    app.highlightWidth = 31;
    app.fontSize = 35;
    changeThickness(9);
    if (app.thickness != 13 || !app.toolPreferencesDirty || app.highlightWidth != 31 ||
        app.fontSize != 35)
        throw std::runtime_error(
            "Changing pen size did not mark preferences dirty or affected other sizes.");
    if (!saveToolPreferences())
        throw std::runtime_error("Could not save pen-size preferences.");
    app.thickness = 4;
    loadToolPreferences();
    if (app.thickness != 13 || app.toolPreferencesDirty || app.highlightWidth != 31 ||
        app.fontSize != 35)
        throw std::runtime_error("Pen size did not survive saving and reloading.");
    WritePrivateProfileStringW(L"ToolPreferences", L"StrokeWidth", L"0", app.iniPath.c_str());
    loadToolPreferences();
    if (app.thickness != 1)
        throw std::runtime_error("Saved pen size was not clamped to the minimum.");
    WritePrivateProfileStringW(L"ToolPreferences", L"StrokeWidth", L"999", app.iniPath.c_str());
    loadToolPreferences();
    if (app.thickness != 100)
        throw std::runtime_error("Saved pen size was not clamped to the maximum.");
    changeThickness(1);
    if (app.toolPreferencesDirty)
        throw std::runtime_error("An unchanged pen size caused a preference write.");
    app.thickness = 40;
    changeThickness(60);
    if (app.thickness != 100 || !app.toolPreferencesDirty || !saveToolPreferences())
        throw std::runtime_error("Pen width did not grow past 40px and save at 100px.");
    app.thickness = 4;
    loadToolPreferences();
    if (app.thickness != 100 || app.toolPreferencesDirty)
        throw std::runtime_error("100px pen width did not survive preference reload.");
    // Re-sizing a selected stroke must remember the resulting size, even when
    // its previous width differs from the toolbar's remembered width.
    app.thickness = 4;
    Annotation stroke;
    stroke.points = {{10, 10}, {100, 20}};
    stroke.thickness = 12;
    app.document.items = {stroke};
    app.document.selected = 0;
    changeThickness(1);
    if (app.thickness != 13 || !app.toolPreferencesDirty || app.document.items[0].thickness != 13)
        throw std::runtime_error("Resizing a selected stroke did not remember the new pen size.");
    if (!app.document.undo() || app.document.items[0].thickness != 12 || app.thickness != 13)
        throw std::runtime_error("Stroke resize undo affected the remembered pen size.");
    app.document.clear();
    changeColor(rgb(12, 34, 56));
    if (!saveToolPreferences())
        throw std::runtime_error("Could not save the pen restart fixture.");
}
struct RenderingDialogTest
{
    bool software, handled = false;
    int button;
    const wchar_t *preview;
    std::string error;
};
Bitmap renderNativeWindow(HWND window)
{
    RECT bounds{};
    GetWindowRect(window, &bounds);
    auto result = Bitmap::create(bounds.right - bounds.left, bounds.bottom - bounds.top);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = result.width;
    info.bmiHeader.biHeight = -result.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP surface = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !surface)
    {
        if (surface)
            DeleteObject(surface);
        if (dc)
            DeleteDC(dc);
        throwWindowsError("Cannot create native dialog preview.");
    }
    const auto previous = SelectObject(dc, surface);
    const bool painted = PrintWindow(window, dc, PW_RENDERFULLCONTENT) != FALSE;
    if (painted)
    {
        std::memcpy(result.pixels.data(), pixels, result.pixels.size());
        for (size_t i = 3; i < result.pixels.size(); i += 4)
            result.pixels[i] = 255;
    }
    SelectObject(dc, previous);
    DeleteObject(surface);
    DeleteDC(dc);
    if (!painted)
        throw std::runtime_error("Cannot render native dialog preview.");
    return result;
}
HRESULT CALLBACK renderingDialogTestCallback(HWND hwnd, UINT notification, WPARAM elapsed, LPARAM,
                                             LONG_PTR context)
{
    return callbackBoundary<HRESULT>(
        [&]() -> HRESULT {
            auto &test = *reinterpret_cast<RenderingDialogTest *>(context);
            if (notification == TDN_CREATED)
                SendMessageW(hwnd, TDM_CLICK_VERIFICATION, test.software, TRUE);
            if (notification == TDN_TIMER && elapsed >= 200 && !test.handled)
            {
                test.handled = true;
                try
                {
                    saveBytes(test.preview, app.graphics.png(renderNativeWindow(hwnd)));
                }
                catch (const std::exception &exception)
                {
                    test.error = exception.what();
                }
                SendMessageW(hwnd, TDM_CLICK_BUTTON, test.error.empty() ? test.button : IDCANCEL,
                             0);
            }
            return S_OK;
        },
        [&](const char *failure) {
            showError(hwnd, failure);
            SendMessageW(hwnd, TDM_CLICK_BUTTON, IDCANCEL, 0);
        },
        E_FAIL);
}
void testRenderingSettings()
{
    const bool originalMode = app.softwareRendering;
    auto dialog = [&](bool software, int button, const wchar_t *preview) {
        RenderingDialogTest test{software, false, button, preview, {}};
        openRenderingSettings(renderingDialogTestCallback, reinterpret_cast<LONG_PTR>(&test));
        if (!test.handled || !test.error.empty())
            throw std::runtime_error("Rendering dialog test failed: " + test.error);
    };
    if (GetMenuState(GetMenu(app.window), RenderingSettings, MF_BYCOMMAND) == static_cast<UINT>(-1))
        throw std::runtime_error("Rendering settings are missing from the menu.");
    if (!setSoftwareRendering(false))
        throw std::runtime_error("Cannot establish rendering test settings.");
    dialog(true, IDOK, L"smoke-test-rendering-software.png");
    if (!app.softwareRendering || app.rendererSpecified ||
        preferenceUInt(app.iniPath, L"Settings", L"SoftwareRendering", 0) != 1)
        throw std::runtime_error("Applying software rendering did not switch and save it.");
    dialog(false, IDCANCEL, L"smoke-test-rendering-cancel.png");
    if (!app.softwareRendering ||
        preferenceUInt(app.iniPath, L"Settings", L"SoftwareRendering", 0) != 1)
        throw std::runtime_error("Cancelling rendering settings changed the mode or preference.");
    app.image = Bitmap::create(480, 320);
    for (size_t i = 0; i < app.image.pixels.size(); i += 4)
    {
        app.image.pixels[i] = app.image.pixels[i + 1] = app.image.pixels[i + 2] = 245;
        app.image.pixels[i + 3] = 255;
    }
    Annotation pen;
    pen.points = {{20, 20}, {100, 120}, {160, 40}};
    app.document.begin();
    app.document.items.push_back(pen);
    app.document.commit();
    app.document.selected = 0;
    app.dirty = true;
    repaint();
    UpdateWindow(app.window);
    const auto source = app.image.pixels;
    const auto items = app.document.items;
    const auto exported = app.graphics.exportImage(app.image, items, app.exportOptions).pixels;
    const auto *cachedPreview = app.previewImage.pixels.data();
    const auto view = app.view;
    dialog(false, IDOK, L"smoke-test-rendering-hardware.png");
    if (app.softwareRendering ||
        preferenceUInt(app.iniPath, L"Settings", L"SoftwareRendering", 1) != 0)
        throw std::runtime_error("Applying hardware rendering did not switch and save it.");
    if (app.image.pixels != source || app.document.items != items || app.document.selected != 0 ||
        !app.dirty || app.previewImage.pixels.data() != cachedPreview ||
        app.view.scale != view.scale || app.view.origin != view.origin ||
        app.graphics.exportImage(app.image, app.document.items, app.exportOptions).pixels !=
            exported ||
        !app.document.undo() || !app.document.items.empty() || !app.document.redo() ||
        app.document.items != items)
        throw std::runtime_error(
            "Switching renderers changed the snip, annotations, history, view, or export.");
    if (!setSoftwareRendering(originalMode))
        throw std::runtime_error("Cannot restore rendering test settings.");
    releaseImage();
    repaint();
    UpdateWindow(app.window);
}
const std::vector<Color> PersistenceTestPalette = {
    Palette[0], Palette[1], Palette[2], Palette[3],      Palette[4],
    Palette[5], Palette[6], Palette[7], rgb(12, 34, 56), rgb(98, 76, 54)};
struct PaletteDialogTest
{
    bool cancel = false;
    const wchar_t *preview = nullptr;
    std::string error;
} paletteDialogTest;
void drivePaletteDialog(HWND window)
{
    try
    {
        SetDlgItemTextW(window, 11, L"#ZZ1122");
        if (IsWindowEnabled(GetDlgItem(window, IDOK)))
            throw std::runtime_error("Invalid hex color was accepted.");
        SetDlgItemTextW(window, 11, L"#123456");
        BOOL valid = FALSE;
        if (!IsWindowEnabled(GetDlgItem(window, IDOK)) ||
            GetDlgItemInt(window, 12, &valid, FALSE) != 18 || !valid)
            throw std::runtime_error("Hex did not update RGB values.");
        SetDlgItemTextW(window, 12, L"256");
        if (IsWindowEnabled(GetDlgItem(window, IDOK)))
            throw std::runtime_error("Out-of-range RGB color was accepted.");
        SetDlgItemTextW(window, 12, L"128");
        wchar_t hex[16]{};
        GetDlgItemTextW(window, 11, hex, 16);
        if (std::wstring(hex) != L"#803456")
            throw std::runtime_error("RGB did not update the hex value.");
        HWND spectrum = GetDlgItem(window, 10);
        RECT bounds{};
        GetClientRect(spectrum, &bounds);
        SendDlgItemMessageW(window, 15, TBM_SETPOS, TRUE, 120);
        SendMessageW(window, WM_HSCROLL, TB_THUMBPOSITION,
                     reinterpret_cast<LPARAM>(GetDlgItem(window, 15)));
        SendMessageW(spectrum, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bounds.right - 1, 0));
        SendMessageW(spectrum, WM_LBUTTONUP, 0, MAKELPARAM(bounds.right - 1, 0));
        GetDlgItemTextW(window, 11, hex, 16);
        if (std::wstring(hex) != L"#00FF00" || GetCapture() == spectrum)
            throw std::runtime_error("Spectrum/hue selection or pointer release failed.");
        SetDlgItemTextW(window, 11, L"#0C2238");
        if (paletteDialogTest.preview)
        {
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            saveBytes(paletteDialogTest.preview, app.graphics.png(renderNativeWindow(window)));
        }
    }
    catch (const std::exception &exception)
    {
        paletteDialogTest.error = exception.what();
    }
    SendMessageW(window, WM_COMMAND,
                 paletteDialogTest.cancel || !paletteDialogTest.error.empty() ? IDCANCEL : IDOK, 0);
}
void testPaletteTools()
{
    const auto originalIni = app.iniPath;
    const auto originalColors = app.colors;
    const auto originalTool = app.tool;
    const float originalDpi = app.dpi;
    RECT originalWindow{};
    GetWindowRect(app.window, &originalWindow);
    app.iniPath =
        (std::filesystem::path(originalIni).parent_path() / L"palette-settings.ini").wstring();
    WritePrivateProfileStringW(L"Palette", nullptr, nullptr, app.iniPath.c_str());
    loadToolPreferences();
    if (app.palette != std::vector<Color>(Palette.begin(), Palette.end()) || app.paletteDirty)
        throw std::runtime_error("Default palette migration failed.");
    app.image = Bitmap::create(640, 360);
    std::fill(app.image.pixels.begin(), app.image.pixels.end(), 255);
    for (float dpi : {1.0f, 1.5f, 2.0f})
    {
        app.dpi = dpi;
        SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(1000 * dpi),
                     static_cast<int>(700 * dpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        auto clickColor = [&](int index) {
            buildButtons();
            const auto button =
                std::find_if(app.buttons.begin(), app.buttons.end(),
                             [&](const Button &b) { return b.command == ColorFirst + index; });
            if (button == app.buttons.end())
                throw std::runtime_error("Palette swatch is missing.");
            const auto point = MAKELPARAM(static_cast<int>((button->rect.left + 12) * dpi),
                                          static_cast<int>((button->rect.top + 12) * dpi));
            SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point);
            SendMessageW(app.window, WM_LBUTTONUP, 0, point);
        };
        app.document.clear();
        selectTool(Tool::Select);
        const auto unchanged = renderedExport().pixels;
        clickColor(4);
        if (app.tool != Tool::Pen || activeColor() != Palette[4] || app.document.canUndo() ||
            renderedExport().pixels != unchanged)
            throw std::runtime_error(
                "Selecting a color with no selected annotation must activate Pen without editing.");
        selectTool(Tool::Select);
        clickColor(4);
        if (app.tool != Tool::Pen || activeColor() != Palette[4])
            throw std::runtime_error(
                "Clicking the remembered Pen color must still switch Select to Pen.");
        Annotation shape;
        shape.kind = Tool::Circle;
        shape.a = {20, 20};
        shape.b = {100, 100};
        app.document.items = {shape};
        selectTool(Tool::Select);
        app.document.selected = 0;
        clickColor(3);
        if (app.tool != Tool::Select || app.document.selected != 0 ||
            app.document.items[0].color != Palette[3] ||
            app.colors[static_cast<size_t>(Tool::Circle)] != Palette[3] ||
            app.colors[static_cast<size_t>(Tool::Pen)] != Palette[4] || !app.document.undo() ||
            app.document.items[0] != shape || app.document.canUndo())
            throw std::runtime_error(
                "Selected annotation recoloring must retain Select and undo in one step.");
    }
    app.document.clear();
    app.dpi = originalDpi;
    selectTool(Tool::Pen);
    Annotation item;
    item.points = {{20, 20}, {150, 70}};
    app.document.items.push_back(item);
    app.document.selected = 0;
    const auto exportedBefore = renderedExport();
    const auto paletteBefore = app.palette;
    paletteDialogTest = {true, L"palette-picker-cancel.png", {}};
    customColor(drivePaletteDialog);
    if (!paletteDialogTest.error.empty())
        throw std::runtime_error(paletteDialogTest.error);
    if (app.palette != paletteBefore || app.document.items[0].color != item.color ||
        app.document.canUndo())
        throw std::runtime_error("Cancel changed the palette, annotation or history.");
    paletteDialogTest = {false, L"palette-picker-add.png", {}};
    customColor(drivePaletteDialog);
    if (!paletteDialogTest.error.empty())
        throw std::runtime_error(paletteDialogTest.error);
    if (app.palette.size() != 9 || app.palette.back() != rgb(12, 34, 56) ||
        app.document.items[0].color != app.palette.back() || !app.document.canUndo())
        throw std::runtime_error("Adding a color did not add a swatch and recolor with undo.");
    customColor(drivePaletteDialog);
    if (app.palette.size() != 9)
        throw std::runtime_error("Adding an existing color created a duplicate swatch.");
    app.document.undo();
    if (renderedExport().pixels != exportedBefore.pixels)
        throw std::runtime_error("Undo failed to restore export pixels after adding a color.");
    app.document.selected = -1;
    const auto unchangedExport = renderedExport().pixels;
    const Color currentColor = activeColor();
    deletePaletteColor(8);
    if (activeColor() != currentColor || renderedExport().pixels != unchangedExport)
        throw std::runtime_error(
            "Deleting a swatch altered the active color or existing annotations.");
    paletteDialogTest = {false, L"palette-picker-edit.png", {}};
    const auto edited =
        pickPaletteColor(app.instance, app.window, app.palette[0], true, drivePaletteDialog);
    if (!paletteDialogTest.error.empty())
        throw std::runtime_error(paletteDialogTest.error);
    if (!edited)
        throw std::runtime_error("Editing a palette color was canceled unexpectedly.");
    editPaletteColor(0, *edited);
    if (app.palette[0] != *edited || app.palette.size() != 8)
        throw std::runtime_error("Editing a swatch did not replace it in place.");
    editPaletteColor(0, app.palette[1]);
    if (app.palette.size() != 7)
        throw std::runtime_error("Editing to an existing color did not merge the duplicate.");
    app.document.clear();
    beginTextEditing({40, 40});
    SendMessageW(app.textEdit, WM_CHAR, L'X', 0);
    const HWND typing = app.textEdit;
    paletteDialogTest = {true, nullptr, {}};
    customColor(drivePaletteDialog);
    if (!paletteDialogTest.error.empty())
        throw std::runtime_error(paletteDialogTest.error);
    if (app.textEdit != typing || app.document.items[0].text != L"X")
        throw std::runtime_error("Canceling the picker interrupted text editing.");
    paletteDialogTest = {false, nullptr, {}};
    customColor(drivePaletteDialog);
    if (!paletteDialogTest.error.empty())
        throw std::runtime_error(paletteDialogTest.error);
    if (app.textEdit != typing || app.document.items[0].color != rgb(12, 34, 56))
        throw std::runtime_error(
            "Adding a palette color interrupted text editing or failed to recolor text.");
    finishTextEditing();
    command(Undo);
    if (!app.document.items.empty())
        throw std::runtime_error(
            "Typing and palette recoloring did not remain one undoable text edit.");
    for (size_t count : {4U, 10U, 18U, 0U})
    {
        while (app.palette.size() > count)
            deletePaletteColor(app.palette.size() - 1);
        while (app.palette.size() < count)
            addPaletteColor(rgb(static_cast<unsigned>(app.palette.size()), 67, 89));
        const auto expected = app.palette;
        if (!saveToolPreferences())
            throw std::runtime_error("Palette preferences could not be saved.");
        app.palette.assign(Palette.begin(), Palette.end());
        loadToolPreferences();
        if (app.palette != expected || app.paletteDirty)
            throw std::runtime_error("Palette count/order/colors did not survive save and reload.");
        for (float dpi : {1.0f, 1.5f, 2.0f})
            for (Tool tool : {Tool::Pen, Tool::Text})
            {
                app.dpi = dpi;
                app.tool = tool;
                SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(850 * dpi),
                             static_cast<int>(560 * dpi),
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                buildButtons();
                const auto formatting = toolbarLayout().formatting;
                for (const auto &button : app.buttons)
                {
                    if (!paletteCommand(button.command) && button.command != CustomColor &&
                        button.command != Eyedropper)
                        continue;
                    if (button.rect.right >= formatting.left ||
                        button.rect.bottom >= canvasRect().top)
                        throw std::runtime_error(
                            "Palette overlaps formatting controls or screenshot workspace.");
                }
                if (std::count_if(app.buttons.begin(), app.buttons.end(), [](const Button &b) {
                        return paletteCommand(b.command);
                    }) != static_cast<int>(count))
                    throw std::runtime_error("Not every saved color has a visible toolbar swatch.");
                if (count)
                {
                    const auto &button =
                        *std::find_if(app.buttons.begin(), app.buttons.end(), [&](const Button &b) {
                            return b.command == ColorFirst + static_cast<int>(count) - 1;
                        });
                    const auto point = MAKELPARAM(static_cast<int>((button.rect.left + 12) * dpi),
                                                  static_cast<int>((button.rect.top + 12) * dpi));
                    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point);
                    SendMessageW(app.window, WM_LBUTTONUP, 0, point);
                    if (activeColor() != expected.back())
                        throw std::runtime_error(
                            "Wrapped palette swatch selected the wrong color.");
                }
                if (count == 18 && tool == Tool::Text)
                    saveBytes((L"palette-toolbar-" + std::to_wstring(static_cast<int>(dpi * 100)) +
                               L".png")
                                  .c_str(),
                              app.graphics.png(renderEditorPreview()));
            }
    }
    // Zero colors is a saved choice; invalid entries must never become colors or command IDs.
    WritePrivateProfileStringW(L"Palette", L"Count", L"3", app.iniPath.c_str());
    WritePrivateProfileStringW(L"Palette", L"0", L"-1", app.iniPath.c_str());
    WritePrivateProfileStringW(L"Palette", L"1", L"255", app.iniPath.c_str());
    WritePrivateProfileStringW(L"Palette", L"2", L"255", app.iniPath.c_str());
    loadToolPreferences();
    if (app.palette != std::vector<Color>{255})
        throw std::runtime_error("Invalid or duplicate saved swatches were not filtered.");
    app.iniPath = originalIni;
    app.dpi = originalDpi;
    app.palette.assign(Palette.begin(), Palette.end());
    app.paletteDirty = false;
    app.colors = originalColors;
    app.tool = originalTool;
    app.toolPreferencesDirty = false;
    releaseImage();
    SetWindowPos(app.window, nullptr, originalWindow.left, originalWindow.top,
                 originalWindow.right - originalWindow.left,
                 originalWindow.bottom - originalWindow.top, SWP_NOZORDER | SWP_NOACTIVATE);
}
void testHighlightTool()
{
    const auto originalColors = app.colors;
    const auto originalWidth = app.highlightWidth;
    const auto originalThickness = app.thickness;
    const auto originalIni = app.iniPath;
    const auto originalOptions = app.exportOptions;
    if (app.colors[static_cast<size_t>(Tool::Highlight)] != Palette[2] || app.highlightWidth != 24)
        throw std::runtime_error("Highlight must start yellow with a broad nib.");
    app.exportOptions = {};
    app.image = Bitmap::create(640, 360);
    std::fill(app.image.pixels.begin(), app.image.pixels.end(), 255);
    Annotation label;
    label.kind = Tool::Text;
    label.color = Ink;
    label.a = {40, 100};
    label.text = L"Highlight this text with a chisel brush";
    app.graphics.measureText(label);
    app.image = app.graphics.flatten(app.image, {label});
    app.fit = true;
    updateView();
    buildButtons();
    const auto button = std::find_if(app.buttons.begin(), app.buttons.end(),
                                     [](const Button &b) { return b.command == HighlightTool; });
    if (button == app.buttons.end() || !enabled(HighlightTool))
        throw std::runtime_error("Highlight toolbar control is missing.");
    auto toolbarPoint =
        MAKELPARAM(static_cast<int>((button->rect.left + button->rect.right) / 2 * app.dpi),
                   static_cast<int>((button->rect.top + button->rect.bottom) / 2 * app.dpi));
    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, toolbarPoint);
    SendMessageW(app.window, WM_LBUTTONUP, 0, toolbarPoint);
    if (!active(HighlightTool) || activeColor() != Palette[2] || brushWidth() != 24)
        throw std::runtime_error("Highlight toolbar did not select its own color and width.");
    auto point = [&](Point p) {
        updateView();
        p = app.view.toScreen(p) * app.dpi;
        return MAKELPARAM(static_cast<int>(std::round(p.x)), static_cast<int>(std::round(p.y)));
    };
    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point({40, 117}));
    SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, point({240, 117}));
    SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, point({520, 117}));
    SendMessageW(app.window, WM_LBUTTONUP, 0, point({520, 117}));
    if (app.tool != Tool::Highlight || selected() || app.document.items.size() != 1 ||
        app.document.items[0].kind != Tool::Highlight || app.document.items[0].points.size() != 3 ||
        app.document.items[0].thickness != 24 || app.document.items[0].color != Palette[2])
        throw std::runtime_error(
            "Highlight drag did not create a persistent freehand chisel stroke.");
    const auto drawn = app.document.items;
    const auto exported = renderedExport();
    if (!app.document.undo() || !app.document.items.empty() || !app.document.redo() ||
        app.document.items != drawn || renderedExport().pixels != exported.pixels ||
        app.graphics.decode(app.graphics.png(exported)).pixels != exported.pixels)
        throw std::runtime_error("Highlight undo/redo or export changed the stroke.");
    repaint();
    UpdateWindow(app.window);
    saveBytes(L"smoke-test-highlight.png", app.graphics.png(renderEditorPreview()));
    const auto pixels = chiselCursorPixels(24, Palette[2]);
    if (pixels.sample({15, 15}) != Palette[2] || pixels.pixels[3] != 0)
        throw std::runtime_error("Chisel cursor color or transparent edges failed.");
    selectTool(Tool::Select);
    app.document.selected = app.document.hit({250, 117}, 0);
    if (!selected())
        throw std::runtime_error("Highlight cannot be selected.");
    changeColor(Palette[3]);
    if (app.document.items[0].color != Palette[3] ||
        app.colors[static_cast<size_t>(Tool::Highlight)] != Palette[3] ||
        app.colors[static_cast<size_t>(Tool::Pen)] !=
            originalColors[static_cast<size_t>(Tool::Pen)])
        throw std::runtime_error("Recoloring a highlight lost its preference or changed the pen.");
    selectTool(Tool::Pen);
    SendMessageW(app.window, WM_KEYDOWN, 'H', 0);
    if (app.tool != Tool::Highlight || activeColor() != Palette[3])
        throw std::runtime_error("Highlight shortcut or remembered color failed.");
    changeThickness(6);
    if (brushWidth() != 30 || app.thickness != originalThickness)
        throw std::runtime_error("Highlight width leaked into the pen width.");
    const auto beforeCancel = app.document.items;
    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point({50, 200}));
    SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, point({300, 220}));
    SendMessageW(app.window, WM_KEYDOWN, VK_ESCAPE, 0);
    if (app.document.items != beforeCancel || app.document.editing() || app.drag != Drag::None)
        throw std::runtime_error("Canceling a highlight kept an incomplete stroke.");
    app.iniPath = (std::filesystem::current_path() / L"highlight-tool-settings.ini").wstring();
    if (!saveToolPreferences())
        throw std::runtime_error("Highlight preferences could not be saved.");
    app.colors[static_cast<size_t>(Tool::Highlight)] = Palette[2];
    app.highlightWidth = 24;
    loadToolPreferences();
    if (app.colors[static_cast<size_t>(Tool::Highlight)] != Palette[3] || app.highlightWidth != 30)
        throw std::runtime_error("Highlight color and width did not survive preference reload.");
    app.iniPath = originalIni;
    app.colors = originalColors;
    app.highlightWidth = originalWidth;
    app.exportOptions = originalOptions;
    app.toolPreferencesDirty = false;
    releaseImage();
    selectTool(Tool::Select);
}
void testEraserTool()
{
    const auto originalOptions = app.exportOptions;
    const auto originalTool = app.tool;
    const auto originalColors = app.colors;
    const auto originalStyles = app.styles;
    const float originalDpi = app.dpi;
    RECT originalBounds{};
    GetWindowRect(app.window, &originalBounds);
    app.exportOptions = {};
    app.image = Bitmap::create(640, 360);
    std::fill(app.image.pixels.begin(), app.image.pixels.end(), 255);
    app.document.clear();
    selectTool(Tool::Pen);
    buildButtons();
    auto point = [&](Point p) {
        const auto screen = app.view.toScreen(p) * app.dpi;
        return MAKELPARAM(static_cast<int>(std::lround(screen.x)),
                          static_cast<int>(std::lround(screen.y)));
    };
    auto click = [&](Point p) {
        SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point(p));
        SendMessageW(app.window, WM_LBUTTONUP, 0, point(p));
        if (GetCapture() == app.window || app.document.editing() || app.drag != Drag::None)
            throw std::runtime_error("Eraser release left capture or history pending.");
    };
    const auto found = std::find_if(app.buttons.begin(), app.buttons.end(),
                                    [](const Button &b) { return b.command == EraserTool; });
    if (found == app.buttons.end())
        throw std::runtime_error("Eraser toolbar button is missing.");
    const auto &button = *found;
    const auto toolbarPoint = MAKELPARAM(static_cast<int>((button.rect.left + 18) * app.dpi),
                                         static_cast<int>((button.rect.top + 18) * app.dpi));
    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, toolbarPoint);
    SendMessageW(app.window, WM_LBUTTONUP, 0, toolbarPoint);
    if (!active(EraserTool) || active(SelectTool) || !enabled(EraserTool))
        throw std::runtime_error("Eraser toolbar button did not activate independently of Select.");
    buildButtons();
    if (std::any_of(app.buttons.begin(), app.buttons.end(),
                    [](const Button &b) { return b.command == SizeDown || b.command == SizeUp; }))
        throw std::runtime_error("Whole-object eraser incorrectly offers stroke-size controls.");
    Annotation older;
    older.points = {{30, 100}, {150, 100}, {240, 140}};
    Annotation keep = older;
    keep.move({0, 140});
    keep.color = Palette[4];
    app.document.items = {older, keep};
    updateView();
    const auto before = renderedExport();
    saveBytes(L"eraser-before.png", app.graphics.png(renderEditorPreview()));
    click({70, 100});
    if (app.document.items != std::vector<Annotation>{keep} || !app.erasing ||
        app.image.pixels != std::vector<uint8_t>(app.image.pixels.size(), 255))
        throw std::runtime_error("Eraser did not remove the older whole stroke while keeping the "
                                 "newer stroke and screenshot.");
    saveBytes(L"eraser-after.png", app.graphics.png(renderEditorPreview()));
    command(Undo);
    if (app.document.items != std::vector<Annotation>{older, keep} ||
        renderedExport().pixels != before.pixels)
        throw std::runtime_error("Undo erasing did not restore annotations/export pixels.");
    command(Redo);
    if (app.document.items != std::vector<Annotation>{keep})
        throw std::runtime_error("Redo erasing failed.");
    command(Undo);
    const auto blankBefore = app.document.items;
    click({600, 330});
    if (app.document.items != blankBefore || !app.document.canRedo())
        throw std::runtime_error(
            "Erasing blank screenshot pixels changed annotations or cleared redo.");
    Annotation newest = older;
    newest.color = Palette[3];
    app.document.clear();
    app.document.items = {older, newest};
    click({70, 100});
    if (app.document.items != std::vector<Annotation>{older})
        throw std::runtime_error("One eraser click removed multiple overlapping annotations.");
    // A sparse drag crosses two annotations between pointer events and misses the kept one.
    Annotation line;
    line.kind = Tool::Line;
    line.a = {330, 80};
    line.b = {330, 140};
    line.thickness = 1;
    Annotation arrow = line;
    arrow.kind = Tool::Arrow;
    arrow.a = {400, 100};
    arrow.b = {460, 100};
    const std::vector<Annotation> sweep = {older, keep, line, arrow};
    app.document.clear();
    app.document.items = sweep;
    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point({70, 100}));
    SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, point({550, 100}));
    SendMessageW(app.window, WM_LBUTTONUP, 0, point({550, 100}));
    if (app.document.items != std::vector<Annotation>{keep})
        throw std::runtime_error("Fast eraser drag skipped annotations between pointer events.");
    command(Undo);
    if (app.document.items != sweep)
        throw std::runtime_error("An eraser drag did not undo in one step.");
    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point({70, 100}));
    SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, point({550, 100}));
    SendMessageW(app.window, WM_KEYDOWN, VK_ESCAPE, 0);
    if (app.document.items != sweep || app.drag != Drag::None || app.document.editing() ||
        !app.erasing)
        throw std::runtime_error("Esc did not restore a pending erase gesture.");
    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point({70, 100}));
    ReleaseCapture();
    if (app.document.items != sweep || app.document.editing())
        throw std::runtime_error("Losing pointer capture did not cancel erasing.");
    for (Tool kind : {Tool::Pen, Tool::Highlight, Tool::Text, Tool::Circle, Tool::Rectangle,
                      Tool::Arrow, Tool::Check, Tool::Line})
        for (int style = 0; style < StyleCounts[static_cast<size_t>(kind)]; ++style)
        {
            Annotation item;
            item.kind = kind;
            item.style = static_cast<uint8_t>(style);
            item.a = {50, 50};
            item.b = {200, 120};
            item.points = {{50, 80}, {100, 80}, {200, 80}};
            item.text = L"Whole text annotation";
            item.thickness = kind == Tool::Highlight ? 24 : 4;
            app.document.clear();
            app.document.items = {item, keep};
            click(kind == Tool::Pen || kind == Tool::Highlight ? Point{100, 80}
                  : kind == Tool::Arrow                        ? item.arrowSpine(.5f)
                                                               : Point{125, 85});
            if (app.document.items != std::vector<Annotation>{keep})
                throw std::runtime_error("Eraser did not remove a complete annotation type/style.");
        }
    for (float dpi : {1.0f, 1.5f, 2.0f})
    {
        app.dpi = dpi;
        for (int width : {850, 980, 1050})
        {
            SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(width * dpi),
                         static_cast<int>(700 * dpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            buildButtons();
            const auto layout = toolbarLayout();
            if (layout.draw.right >= layout.shapes.left)
                throw std::runtime_error("Adding the eraser made the tool groups overlap.");
            for (const auto &b : app.buttons)
                if (b.command == EraserTool &&
                    (b.rect.right > layout.draw.right || b.rect.left < layout.draw.left))
                    throw std::runtime_error("Eraser did not fit inside the Draw group.");
        }
        for (float zoom : {.5f, 1.0f, 8.0f})
        {
            command(Actual);
            app.fit = false;
            app.view.scale = zoom / dpi;
            const auto r = canvasRect();
            const Point screen{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
            app.view.origin = screen - Point{70, 100} * app.view.scale;
            app.document.clear();
            app.document.items = {older, keep};
            if (editorCursor(screen) != currentEraserCursor() ||
                currentEraserCursor() != app.eraserCursor)
                throw std::runtime_error(
                    "Eraser cursor was not shown or cached across zoom levels.");
            click({70, 100});
            if (app.document.items != std::vector<Annotation>{keep})
                throw std::runtime_error("Eraser hit testing failed at scaled DPI/zoom.");
        }
        app.fit = true;
        updateView();
    }
    SendMessageW(app.window, WM_KEYDOWN, 'E', 0);
    if (app.erasing)
        throw std::runtime_error("E did not toggle eraser off.");
    SendMessageW(app.window, WM_KEYDOWN, 'E', 0);
    if (!app.erasing)
        throw std::runtime_error("E did not select eraser.");
    command(PenTool);
    if (app.erasing || !active(PenTool))
        throw std::runtime_error("Selecting Pen did not leave eraser mode.");
    app.dpi = originalDpi;
    app.exportOptions = originalOptions;
    app.colors = originalColors;
    app.styles = originalStyles;
    releaseImage();
    selectTool(originalTool);
    SetWindowPos(app.window, nullptr, originalBounds.left, originalBounds.top,
                 originalBounds.right - originalBounds.left,
                 originalBounds.bottom - originalBounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    buildButtons();
}
void testCurvedArrowControls()
{
    const auto originalOptions = app.exportOptions;
    const auto originalTool = app.tool;
    const float originalDpi = app.dpi;
    RECT originalBounds{};
    GetWindowRect(app.window, &originalBounds);
    const auto colors = app.colors;
    const auto styles = app.styles;
    const bool preferencesDirty = app.toolPreferencesDirty;
    app.exportOptions = {};
    app.image = Bitmap::create(640, 360);
    std::fill(app.image.pixels.begin(), app.image.pixels.end(), 255);
    Annotation curved;
    curved.kind = Tool::Arrow;
    curved.style = 2;
    curved.a = {200, 160};
    curved.b = {400, 200};
    Annotation other = curved;
    other.style = 0;
    other.a = {20, 300};
    other.b = {120, 300};
    auto button = [&](int id) -> Rect {
        buildButtons();
        const auto found = std::find_if(app.buttons.begin(), app.buttons.end(),
                                        [&](const Button &b) { return b.command == id; });
        if (found == app.buttons.end() || !enabled(id))
            throw std::runtime_error("Curved arrow selection control is missing or disabled.");
        return found->rect;
    };
    for (float dpi : {1.0f, 1.5f, 2.0f})
    {
        app.dpi = dpi;
        SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(1000 * dpi),
                     static_cast<int>(700 * dpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        selectTool(Tool::Select);
        for (float zoom : {.5f, 1.0f, 8.0f})
        {
            app.document.clear();
            app.document.items = {curved, other};
            app.document.selected = 0;
            app.fit = false;
            app.view.scale = zoom / dpi;
            const auto canvas = canvasRect();
            app.view.origin = Point{canvas.right / 2, (canvas.top + canvas.bottom) / 2} -
                              curved.arrowSpine(.5f) * app.view.scale;
            updateView();
            const auto before = app.document.items;
            const auto exportedBefore = renderedExport();
            {
                app.document.selected = 0;
                const Rect r = button(FlipCurvedArrow);
                if (std::count_if(app.buttons.begin(), app.buttons.end(), [](const Button &b) {
                        return curvedArrowCommand(b.command);
                    }) != 1)
                    throw std::runtime_error("Curved arrow must show a single Flip control.");
                if (!canvas.contains({r.left, r.top}) || !canvas.contains({r.right, r.bottom}))
                    throw std::runtime_error("Curved arrow controls escaped the canvas.");
                const auto click = MAKELPARAM(static_cast<int>((r.left + r.right) / 2 * dpi),
                                              static_cast<int>((r.top + r.bottom) / 2 * dpi));
                SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, click);
                // Real painting between press and release must retain the button hit target.
                UpdateWindow(app.window);
                SendMessageW(app.window, WM_LBUTTONUP, 0, click);
                if (app.document.items[0] == curved || app.document.items[1] != other ||
                    app.document.items[0].a != curved.a || app.document.items[0].b != curved.b ||
                    app.document.selected != 0 || app.drag != Drag::None || app.pressed ||
                    GetCapture() == app.window || app.document.editing())
                    throw std::runtime_error(
                        "Curved arrow click changed the wrong object or left an edit pending.");
                const auto changed = app.document.items;
                const auto exportedAfter = renderedExport();
                if (exportedBefore.pixels == exportedAfter.pixels ||
                    previewImage().pixels != exportedAfter.pixels ||
                    app.graphics.decode(app.graphics.png(exportedAfter)).pixels !=
                        exportedAfter.pixels ||
                    !changed[0].hit(changed[0].arrowSpine(.5f), 0))
                    throw std::runtime_error(
                        "Curved arrow direction did not update preview, export or hit testing.");
                command(Undo);
                if (app.document.items != before ||
                    renderedExport().pixels != exportedBefore.pixels || app.document.canUndo())
                    throw std::runtime_error("Curved arrow action did not undo in one step.");
                command(Redo);
                if (app.document.items != changed ||
                    renderedExport().pixels != exportedAfter.pixels)
                    throw std::runtime_error("Curved arrow redo did not restore exported pixels.");
                command(Undo);
            }
            app.document.selected = 0;
            if (dpi == 1 && zoom == 1)
                saveBytes(L"curved-arrow-controls.png", app.graphics.png(renderEditorPreview()));
            // Moving the arrow to the top/left edge keeps its controls reachable.
            app.document.items[0].move({-200, -160});
            app.fit = true;
            updateView();
            button(FlipCurvedArrow);
            for (int selection : {-1, 1})
            {
                app.document.selected = selection;
                buildButtons();
                if (std::any_of(app.buttons.begin(), app.buttons.end(),
                                [](const Button &b) { return curvedArrowCommand(b.command); }))
                    throw std::runtime_error(
                        "Curved arrow controls appeared for another selection.");
                const auto unchanged = app.document.items;
                command(FlipCurvedArrow);
                if (app.document.items != unchanged)
                    throw std::runtime_error("A curved arrow action changed another selection.");
            }
        }
    }
    // A rapid second click must use the control even when it overlays a text annotation.
    app.document.items = {curved, other};
    app.document.selected = 0;
    updateView();
    const auto r = button(FlipCurvedArrow);
    Annotation underControl;
    underControl.kind = Tool::Text;
    underControl.text = L"Text beneath selection controls";
    underControl.a = app.view.toImage({r.left, r.top});
    underControl.b = app.view.toImage({r.right, r.bottom});
    app.document.items.push_back(underControl);
    const auto doubleClick = MAKELPARAM(static_cast<int>((r.left + r.right) / 2 * app.dpi),
                                        static_cast<int>((r.top + r.bottom) / 2 * app.dpi));
    SendMessageW(app.window, WM_LBUTTONDBLCLK, MK_LBUTTON, doubleClick);
    SendMessageW(app.window, WM_LBUTTONUP, 0, doubleClick);
    if (app.textEdit || app.document.selected != 0 || app.document.items[0] == curved ||
        app.document.items[2] != underControl)
        throw std::runtime_error("Double-clicking a curved arrow control edited underlying text.");
    if (app.colors != colors || app.styles != styles ||
        app.toolPreferencesDirty != preferencesDirty)
        throw std::runtime_error("Curved arrow controls changed future tool preferences.");
    releaseImage();
    app.exportOptions = originalOptions;
    app.tool = originalTool;
    app.dpi = originalDpi;
    SetWindowPos(app.window, nullptr, originalBounds.left, originalBounds.top,
                 originalBounds.right - originalBounds.left,
                 originalBounds.bottom - originalBounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    buildButtons();
}
void testRecentSnips()
{
    const auto options = app.exportOptions;
    const auto dpi = app.dpi;
    const auto collapsed = app.collapsedRows;
    RECT bounds{};
    GetWindowRect(app.window, &bounds);
    releaseImage();
    app.recent.clear();
    app.recentSequence = 0;
    app.exportOptions = {};
    app.collapsedRows = 0;
    auto require = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    auto capture = [&](int number, int width = 640, int height = 360) {
        auto image = Bitmap::create(width, height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const size_t p = (static_cast<size_t>(y) * width + x) * 4;
                image.pixels[p] = static_cast<uint8_t>(220 + number % 25);
                image.pixels[p + 1] = static_cast<uint8_t>(230 - number % 20);
                image.pixels[p + 2] = static_cast<uint8_t>(245 - number % 30);
                image.pixels[p + 3] = 255;
            }
        Annotation label;
        label.kind = Tool::Text;
        label.a = {24, 20};
        label.text = L"Screenshot " + std::to_wstring(number);
        label.fontSize = 30;
        label.color = Ink;
        app.graphics.measureText(label);
        return app.graphics.flatten(image, {label});
    };
    auto click = [&](int id, bool cancel = false) {
        buildButtons();
        const auto found = std::find_if(app.buttons.begin(), app.buttons.end(),
                                        [&](const Button &b) { return b.command == id; });
        require(found != app.buttons.end() && enabled(id),
                "Recent snip mouse control is unavailable.");
        const auto r = found->rect;
        const auto point = MAKELPARAM(static_cast<int>((r.left + r.right) / 2 * app.dpi),
                                      static_cast<int>((r.top + r.bottom) / 2 * app.dpi));
        SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point);
        UpdateWindow(app.window);
        SendMessageW(app.window, WM_LBUTTONUP, 0, cancel ? MAKELPARAM(2, 2) : point);
        require(!app.pressed && GetCapture() != app.window && app.drag == Drag::None,
                "Recent snip button left a pointer gesture pending.");
    };
    require(!enabled(RecentSnips), "A fresh session has recent captures.");
    auto first = capture(1);
    const auto original = first.pixels;
    acceptCapture(std::move(first));
    require(app.recent.size() == 1 && app.activeRecent == 0 && app.recent[0].image.empty(),
            "The active capture was duplicated in session storage.");
    beginTextEditing({50, 70});
    SendMessageW(app.textEdit, WM_CHAR, 'X', 0);
    click(RecentSnips);
    require(!app.textEdit && app.document.items.size() == 1 && app.document.items[0].text == L"X" &&
                !app.recent[0].thumbnail.empty(),
            "Opening Recent did not commit text and refresh its thumbnail.");
    click(RecentClose);
    applyCrop({40, 40, 600, 300});
    app.fit = false;
    app.view.scale = 2 / app.dpi;
    updateView();
    app.savePath = L"recent-first.png";
    const auto firstItems = app.document.items;
    const auto firstPixels = renderedExport().pixels;
    const auto firstView = app.view;
    acceptCapture(capture(2));
    Annotation arrow;
    arrow.kind = Tool::Arrow;
    arrow.style = 2;
    arrow.a = {120, 140};
    arrow.b = {480, 220};
    app.document.begin();
    app.document.items.push_back(arrow);
    app.document.commit();
    app.dirty = true;
    app.savePath = L"recent-second.png";
    saveImage();
    const auto secondPixels = renderedExport().pixels;
    click(RecentSnips);
    click(RecentChoiceFirst);
    require(app.activeRecent == 0 && app.document.items == firstItems && app.image.width == 560 &&
                app.savePath == L"recent-first.png" && app.dirty &&
                app.view.scale == firstView.scale && app.view.origin == firstView.origin &&
                renderedExport().pixels == firstPixels,
            "Switching captures lost text, crop, save path, dirty state, zoom, or export pixels.");
    command(Undo);
    require(app.image.pixels == original && app.document.items[0].a == Point{50, 70} &&
                app.document.canRedo(),
            "A restored capture lost its crop source or undo history.");
    click(RecentSnips);
    click(RecentChoiceFirst + 1);
    require(app.savePath == L"recent-second.png" && !app.dirty &&
                renderedExport().pixels == secondPixels,
            "A saved capture changed when reopened.");
    restoreRecentSnip(0);
    require(app.document.canRedo(), "Switching snips discarded redo history.");
    command(Redo);
    require(renderedExport().pixels == firstPixels, "Restored crop redo changed export pixels.");
    command(Undo);
    command(Undo);
    require(app.document.items.empty(), "Annotation history leaked across captures.");
    command(Redo);
    command(Redo);
    require(renderedExport().pixels == firstPixels,
            "Restored annotation/crop history did not round trip.");
    const auto count = app.recent.size();
    startSnip(true);
    cancelCapture();
    require(app.recent.size() == count && app.activeRecent == 0 &&
                renderedExport().pixels == firstPixels,
            "Canceling a capture changed the session collection or active snip.");
    const auto savedCount = app.recent.size();
    SendMessageW(app.window, WM_CLOSE, 0, 0);
    require(!hasImage() && app.recent.size() == savedCount && !app.recent[0].image.empty(),
            "Closing to the tray lost recent captures.");
    showEditor();
    click(RecentSnips);
    click(RecentChoiceFirst);
    require(renderedExport().pixels == firstPixels, "Reopening from the tray lost edited pixels.");
    for (int n = 3; n <= 11; ++n)
        acceptCapture(capture(n));
    require(app.recent.size() == 10 && app.recent.front().sequence == 2 &&
                app.recent.back().sequence == 11,
            "The 11th capture did not evict exactly the oldest snip.");
    restoreRecentSnip(0);
    require(renderedExport().pixels == secondPixels && app.recent.front().sequence == 2,
            "Revisiting the oldest snip changed its edits or capture order.");
    acceptCapture(capture(12));
    require(app.recent.size() == 10 && app.recent.front().sequence == 3 &&
                app.recent.back().sequence == 12,
            "Capturing while viewing the oldest snip evicted the wrong entry.");
    for (float scale : {1.0f, 1.5f, 2.0f})
        for (int width : {1000, 850})
        {
            app.dpi = scale;
            SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(width * scale),
                         static_cast<int>((width == 850 ? 430 : 700) * scale),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            command(RecentSnips);
            require(app.recentOpen, "Recent button did not reopen.");
            const auto panel = recentPanelRect(), client = clientDips();
            require(panel.left >= 0 && panel.top >= 0 && panel.right <= client.right &&
                        panel.bottom <= client.bottom - StatusHeight,
                    "Recent popup escaped a small/DPI-scaled window.");
            const auto scaleBefore = app.view.scale;
            POINT wheelPoint{static_cast<LONG>((panel.left + 30) * scale),
                             static_cast<LONG>((panel.top + 60) * scale)};
            ClientToScreen(app.window, &wheelPoint);
            SendMessageW(app.window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                         MAKELPARAM(wheelPoint.x, wheelPoint.y));
            require(app.view.scale == scaleBefore,
                    "Scrolling Recent zoomed the screenshot beneath it.");
            saveBytes(L"recent-panel-" + std::to_wstring(width) + L"-" +
                          std::to_wstring(static_cast<int>(scale * 100)) + L".png",
                      app.graphics.png(renderEditorPreview()));
            processKey(VK_END);
            require(app.recentFocus == 9 && app.recentScroll + recentVisibleRows() >= 5,
                    "Keyboard navigation could not reach the oldest thumbnail.");
            processKey(VK_RETURN);
            require(app.activeRecent == 0 && !app.recentOpen,
                    "Keyboard selection opened the wrong snip.");
            command(RecentSnips);
            processKey(VK_ESCAPE);
            require(!app.recentOpen && app.activeRecent == 0, "Esc changed the active capture.");
            command(RecentSnips);
            const auto before = app.document.items;
            const auto outside = MAKELPARAM(30, static_cast<int>((canvasRect().top + 20) * scale));
            SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, outside);
            SendMessageW(app.window, WM_LBUTTONUP, 0, outside);
            require(!app.recentOpen && app.document.items == before && !app.document.editing(),
                    "Dismissing Recent drew on the screenshot.");
            command(RecentSnips);
            click(RecentClose);
            click(RecentSnips, true);
            require(!app.recentOpen, "A canceled Recent button press opened the panel.");
            for (unsigned mask = 0; mask < 8; ++mask)
            {
                app.collapsedRows = mask;
                command(RecentSnips);
                const auto popup = recentPanelRect();
                require(popup.bottom <= clientDips().bottom - StatusHeight,
                        "Collapsed rows pushed Recent out of a compact window.");
                processKey(VK_ESCAPE);
            }
            app.collapsedRows = 0;
        }
    app.dpi = dpi;
    app.collapsedRows = collapsed;
    SetWindowPos(app.window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                 bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    BYTE keyboard[256]{}, restoreKeyboard[256]{};
    GetKeyboardState(restoreKeyboard);
    std::copy(std::begin(restoreKeyboard), std::end(restoreKeyboard), std::begin(keyboard));
    keyboard[VK_CONTROL] = keyboard[VK_SHIFT] = 0x80;
    SetKeyboardState(keyboard);
    processKey('R');
    SetKeyboardState(restoreKeyboard);
    require(app.recentOpen, "Ctrl+Shift+R did not open Recent.");
    SendMessageW(app.window, WM_ACTIVATE, WA_INACTIVE, 0);
    require(!app.recentOpen, "Recent stayed open when the editor lost activation.");
    for (int row = 0; row < 3; ++row)
    {
        command(ToggleActions + row);
        command(RecentSnips);
        require(app.recentOpen, "Recent menu command failed with collapsed toolbars.");
        processKey(VK_ESCAPE);
    }
    command(FullScreen);
    click(RecentSnips);
    saveBytes(L"recent-full-screen.png", app.graphics.png(renderEditorPreview()));
    processKey(VK_ESCAPE);
    command(FullScreen);
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
    {
        restoreRecentSnip(i % 10);
        UpdateWindow(app.window);
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    std::string timings =
        "Mean switch and paint (640x360, software): " + std::to_string(elapsed / 20) + " ms\n";
    for (const auto &snip : app.recent)
        require(snip.thumbnail.width <= 400 && snip.thumbnail.height <= 224,
                "Recent thumbnails retained full-size render buffers.");
    std::ifstream saved(std::filesystem::path(L"recent-second.png"), std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(saved),
                                     std::istreambuf_iterator<char>()};
    require(app.graphics.decode(bytes).pixels == secondPixels,
            "Saved PNG differed from the restored snip export.");
    releaseImage();
    app.recent.clear();
    for (int n = 0; n < 3; ++n)
        acceptCapture(capture(20 + n, 3840, 2160));
    std::vector<double> switches;
    for (int n = 0; n < 9; ++n)
    {
        const auto start = std::chrono::steady_clock::now();
        restoreRecentSnip(n % 3);
        UpdateWindow(app.window);
        switches.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }
    std::sort(switches.begin(), switches.end());
    timings += "4K switch and paint (software), median: " + std::to_string(switches[4]) +
               " ms; worst: " + std::to_string(switches.back()) + " ms\n";
    command(RecentSnips);
    UpdateWindow(app.window);
    const auto popupStart = std::chrono::steady_clock::now();
    for (int n = 0; n < 10; ++n)
    {
        repaint();
        UpdateWindow(app.window);
    }
    timings += "Mean cached popup repaint (software): " +
               std::to_string(std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - popupStart)
                                  .count() /
                              10) +
               " ms\n";
    writeTestReport(L"recent-timings.txt", timings);
    releaseImage();
    app.recent.clear();
    app.recentSequence = 0;
    app.exportOptions = options;
    app.collapsedRows = collapsed;
    buildButtons();
}
void testCropTool()
{
    const auto options = app.exportOptions;
    const float dpi = app.dpi;
    RECT originalBounds{};
    GetWindowRect(app.window, &originalBounds);
    releaseImage();
    app.exportOptions = {};
    app.image = Bitmap::create(640, 360);
    for (size_t i = 0; i < app.image.pixels.size(); i += 4)
    {
        app.image.pixels[i] = static_cast<uint8_t>(i / 4);
        app.image.pixels[i + 1] = static_cast<uint8_t>(i / (640 * 4));
        app.image.pixels[i + 2] = 80;
        app.image.pixels[i + 3] = 255;
    }
    Annotation label;
    label.kind = Tool::Text;
    label.a = {110, 110};
    label.b = {220, 150};
    label.text = L"Crop keeps annotations";
    app.document.items.push_back(label);
    const auto original = app.image;
    const auto originalExport = renderedExport();
    applyCrop({100, 80, 400, 280});
    if (app.image.pixels != original.crop(100, 80, 300, 200).pixels ||
        app.document.items[0].a != Point{10, 30} ||
        app.document.cropBounds != Rect{100, 80, 400, 280})
        throw std::runtime_error("Editor crop changed screenshot pixels or misplaced annotations.");
    const auto firstExport = renderedExport();
    applyCrop({20, 10, 180, 120});
    if (app.image.pixels != original.crop(120, 90, 160, 110).pixels)
        throw std::runtime_error("Repeated editor crop lost the original screenshot coordinates.");
    command(Undo);
    if (renderedExport().pixels != firstExport.pixels)
        throw std::runtime_error("Undo crop did not restore the previous image and annotations.");
    command(Undo);
    if (app.image.pixels != original.pixels || renderedExport().pixels != originalExport.pixels)
        throw std::runtime_error("Undo crop did not restore the full image exactly.");
    command(Redo);
    if (renderedExport().pixels != firstExport.pixels)
        throw std::runtime_error("Redo crop did not restore exported pixels.");
    command(Undo);
    applyCrop({-20, -20, 640, 360});
    applyCrop({10, 10, 10, 10});
    if (app.image.pixels != original.pixels || app.document.cropBounds)
        throw std::runtime_error("Empty or full-image crop changed the document.");
    for (float scale : {1.0f, 1.5f, 2.0f})
    {
        app.dpi = scale;
        SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(1000 * scale),
                     static_cast<int>(700 * scale), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        app.fit = true;
        updateView();
        auto point = [&](Point p) {
            p = app.view.toScreen(p) * app.dpi;
            return MAKELPARAM(static_cast<int>(std::round(p.x)), static_cast<int>(std::round(p.y)));
        };
        command(CropTool);
        buildButtons(); // Refresh layout after the test changes DPI, selection and image state.
        updateView();
        const auto a = point({380, 250}), b = point({100, 80});
        SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, a);
        SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, b);
        if (app.drag != Drag::Crop || !active(CropTool))
            throw std::runtime_error(
                "Crop tool did not begin a reverse-direction mouse selection.");
        if (scale == 1)
            saveBytes(L"smoke-test-crop-selection.png", app.graphics.png(renderEditorPreview()));
        SendMessageW(app.window, WM_LBUTTONUP, 0, b);
        if (app.cropping || !app.document.cropBounds || app.image.width < 279 ||
            app.image.width > 282 || app.image.height < 169 || app.image.height > 172)
            throw std::runtime_error(
                "Mouse crop dimensions failed at scaled DPI " + std::to_string(scale) + ": " +
                std::to_string(app.image.width) + "x" + std::to_string(app.image.height));
        if (app.graphics.decode(app.graphics.png(renderedExport())).pixels !=
            renderedExport().pixels)
            throw std::runtime_error("Cropped PNG export changed pixels.");
        command(Undo);
        command(CropTool);
        SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point({100, 80}));
        SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, point({380, 250}));
        SendMessageW(app.window, WM_KEYDOWN, VK_ESCAPE, 0);
        if (app.cropping || app.drag != Drag::None || app.image.pixels != original.pixels ||
            app.document.items != std::vector<Annotation>{label})
            throw std::runtime_error("Canceling crop changed the image or annotations.");
    }
    app.dpi = dpi;
    app.exportOptions = options;
    releaseImage();
    SetWindowPos(app.window, nullptr, originalBounds.left, originalBounds.top,
                 originalBounds.right - originalBounds.left,
                 originalBounds.bottom - originalBounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    buildButtons();
}
void testCaptureShortcuts()
{
    const WORD area = MAKEWORD(VK_F20, HOTKEYF_CONTROL | HOTKEYF_ALT);
    const WORD full = MAKEWORD(VK_F21, HOTKEYF_CONTROL | HOTKEYF_ALT);
    if (!registerShortcuts(area, full, false) || !app.hotkeyRegistered ||
        !app.instantHotkeyRegistered)
        throw std::runtime_error("Independent capture shortcuts could not be registered.");
    if (registerShortcuts(area, area, false) || app.hotkey != area || app.instantHotkey != full)
        throw std::runtime_error("Duplicate shortcut settings replaced the working bindings.");
    if (!registerShortcuts(full, area, false))
        throw std::runtime_error("Swapping capture shortcut settings failed.");
    const WORD blocked = MAKEWORD(VK_F22, HOTKEYF_CONTROL | HOTKEYF_ALT);
    if (!RegisterHotKey(app.window, 90, hotkeyModifiers(blocked), LOBYTE(blocked)))
        throw std::runtime_error("Cannot reserve conflicting shortcut test fixture.");
    const bool accepted = registerShortcuts(area, blocked, false);
    UnregisterHotKey(app.window, 90);
    if (accepted || app.hotkey != full || app.instantHotkey != area || !app.hotkeyRegistered ||
        !app.instantHotkeyRegistered)
        throw std::runtime_error("Shortcut conflict did not restore both working bindings.");
    const bool areaAvailable =
        RegisterHotKey(app.window, 91, hotkeyModifiers(full), LOBYTE(full)) != FALSE;
    const bool fullAvailable =
        RegisterHotKey(app.window, 92, hotkeyModifiers(area), LOBYTE(area)) != FALSE;
    if (areaAvailable)
        UnregisterHotKey(app.window, 91);
    if (fullAvailable)
        UnregisterHotKey(app.window, 92);
    if (areaAvailable || fullAvailable)
        throw std::runtime_error("Rollback lost a Windows shortcut registration.");
    if (!registerShortcuts(0, area, false) || app.hotkeyRegistered ||
        !app.instantHotkeyRegistered || !registerShortcuts(full, 0, false) ||
        !app.hotkeyRegistered || app.instantHotkeyRegistered || !registerShortcuts(0, 0, false))
        throw std::runtime_error("Disabling either capture shortcut independently failed.");
    if (!registerShortcuts(area, VK_PAUSE, false) || !app.instantHotkeyRegistered ||
        hotkeyName(app.instantHotkey) != L"Pause" || registerShortcuts(area, 'P', false) ||
        registerShortcuts(area, VK_F12, false) || app.instantHotkey != VK_PAUSE ||
        !registerShortcuts(0, 0, false))
        throw std::runtime_error(
            "Standalone Pause shortcut or ordinary/reserved-key validation failed.");
}
void testShortcutFields()
{
    testCaptureShortcuts();
    app.hotkey = static_cast<WORD>(preferenceUInt(app.iniPath, L"Settings", L"Hotkey", 0));
    app.instantHotkey =
        static_cast<WORD>(preferenceUInt(app.iniPath, L"Settings", L"InstantHotkey", 0));
    openSettings();
    auto expectLabel = [&](WORD key) {
        wchar_t label[128]{};
        GetWindowTextW(app.instantHotkeyControl, label, 128);
        if (std::wstring(label) != hotkeyName(key) ||
            SendMessageW(app.instantHotkeyControl, HKM_GETHOTKEY, 0, 0) != key)
            throw std::runtime_error("Shortcut field label does not match its stored key.");
    };
    expectLabel(app.instantHotkey);
    SendMessageW(app.instantHotkeyControl, WM_KEYDOWN, VK_HOME, (1LL << 24) | (0x47 << 16) | 1);
    expectLabel(MAKEWORD(VK_HOME, HOTKEYF_EXT));
    SendMessageW(app.instantHotkeyControl, WM_KEYDOWN, VK_PAUSE, (0x45 << 16) | 1);
    expectLabel(VK_PAUSE);
    SendMessageW(app.instantHotkeyControl, WM_KEYDOWN, VK_BACK, 1);
    expectLabel(0);
    SendMessageW(app.instantHotkeyControl, WM_KEYDOWN, VK_PAUSE, (0x45 << 16) | 1);
    expectLabel(VK_PAUSE);
    saveBytes(L"shortcut-test-pause.png", app.graphics.png(renderNativeWindow(app.settingsWindow)));
    closeSettings();
    openSettings();
    expectLabel(app.instantHotkey);
    SendMessageW(app.instantHotkeyControl, WM_KEYDOWN, VK_PAUSE, (0x45 << 16) | 1);
    SendMessageW(app.hotkeyControl, HKM_SETHOTKEY, MAKEWORD(VK_F20, HOTKEYF_CONTROL | HOTKEYF_ALT),
                 0);
    SendMessageW(app.settingsWindow, WM_COMMAND, IDOK, 0);
    if (app.settingsWindow || app.instantHotkey != VK_PAUSE ||
        preferenceUInt(app.iniPath, L"Settings", L"InstantHotkey", 0) != VK_PAUSE)
        throw std::runtime_error("Pause shortcut settings did not save.");
    INPUT input[2]{};
    for (auto &event : input)
    {
        event.type = INPUT_KEYBOARD;
        event.ki.wVk = VK_PAUSE;
    }
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(2, input, sizeof(INPUT)) != 2)
        throw std::runtime_error("Windows rejected Pause input.");
    bool delivered = false;
    const auto deadline = GetTickCount64() + 2000;
    MSG message{};
    while (!delivered && GetTickCount64() < deadline)
    {
        if (PeekMessageW(&message, app.window, WM_HOTKEY, WM_HOTKEY, PM_REMOVE))
        {
            if (message.wParam == static_cast<WPARAM>(app.instantHotkeyId))
            {
                DispatchMessageW(&message);
                delivered = true;
            }
        }
        else
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    if (!delivered || app.overlay || app.image.width != GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
        app.image.height != GetSystemMetrics(SM_CYVIRTUALSCREEN))
        throw std::runtime_error("Windows Pause hotkey did not open the instant capture.");
    openSettings();
    expectLabel(VK_PAUSE);
    closeSettings();
}
void testNavigation(HWND window)
{
    const auto originalImage = app.image;
    const auto originalDocument = app.document;
    const auto originalOptions = app.exportOptions;
    const auto originalTool = app.tool;
    const float originalDpi = app.dpi;
    RECT originalBounds{};
    GetWindowRect(window, &originalBounds);
    auto require = [](bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    app.document.clear();
    app.exportOptions = {};
    app.tool = Tool::Select;
    app.image = Bitmap::create(3200, 1800);
    for (int y = 0; y < app.image.height; ++y)
        for (int x = 0; x < app.image.width; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * app.image.width + x) * 4;
            app.image.pixels[i] = static_cast<uint8_t>(80 + x / 32);
            app.image.pixels[i + 1] = static_cast<uint8_t>(100 + y / 18);
            app.image.pixels[i + 2] = 230;
            app.image.pixels[i + 3] = 255;
        }
    resetPreview();
    const auto originalPixels = renderedExport();
    auto center = [] {
        const auto r = navigationRect();
        return Point{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    };
    auto mouse = [](Point p) {
        return MAKELPARAM(static_cast<int>(std::lround(p.x * app.dpi)),
                          static_cast<int>(std::lround(p.y * app.dpi)));
    };
    auto wheel = [&](Point p, int delta, WORD modifiers = 0) {
        POINT physical{static_cast<LONG>(std::lround(p.x * app.dpi)),
                       static_cast<LONG>(std::lround(p.y * app.dpi))};
        ClientToScreen(window, &physical);
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(modifiers, static_cast<WORD>(delta)),
                     MAKELPARAM(physical.x, physical.y));
    };
    auto requireCentered = [&] {
        require(length(app.view.toScreen({1600, 900}) - center()) < .02f,
                "Fitted screenshot did not return to the workspace center.");
    };
    auto cursorPreview = Bitmap::create(288, 144);
    std::fill(cursorPreview.pixels.begin(), cursorPreview.pixels.end(), 240);
    for (size_t i = 3; i < cursorPreview.pixels.size(); i += 4)
        cursorPreview.pixels[i] = 255;
    int cursorColumn = 0;
    for (float d : {1.0f, 1.5f, 2.0f})
    {
        SetWindowPos(window, nullptr, 0, 0, static_cast<int>(1000 * d), static_cast<int>(700 * d),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        app.dpi = d;
        if (app.target)
            app.target->SetDpi(d * 96, d * 96);
        command(Fit);
        const auto minimum = app.view.scale;
        require(minimum * d < 1 && !canPanImage(),
                "Fitted minimum failed to show the whole large screenshot.");
        requireCentered();
        wheel(center(), -120);
        require(app.fit && app.view.scale == minimum, "Wheel zoomed below the fitted minimum.");
        wheel(center(), 120);
        require(!app.fit && std::abs(app.view.scale - minimum * 1.2f) < .0001f,
                "Ordinary wheel did not zoom without Ctrl.");
        zoomAt(center(), 4);
        const Point anchor = center() + Point{-45, 35};
        const auto focus = app.view.toImage(anchor);
        wheel(anchor, 120);
        const Point roundedAnchor{std::round(anchor.x * d) / d, std::round(anchor.y * d) / d};
        require(length(app.view.toImage(roundedAnchor) - focus) < 2,
                "Zoom failed to keep the pointer's image detail in place.");
        require(editorCursor(center()) == currentGrabCursor(false),
                "Select did not show the open hand cursor.");
        require(app.grabCursor[0] && currentGrabCursor(true) != app.grabCursor[0] &&
                    app.grabCursor[1],
                "Native open/closed hand cursors were not created.");
        for (int row = 0; row < 2; ++row)
            cursorPreview = compositeCursor(cursorPreview, currentGrabCursor(row != 0),
                                            cursorColumn * 96 + 16, row * 72 + 4);
        ++cursorColumn;
        const auto beforePan = app.view.origin;
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, mouse(center()));
        require(app.drag == Drag::Pan && editorCursor(center()) == currentGrabCursor(true),
                "Left drag did not grip the zoomed screenshot.");
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, mouse(center() + Point{40, 25}));
        SendMessageW(window, WM_LBUTTONUP, 0, mouse(center() + Point{40, 25}));
        require(length(app.view.origin - beforePan) > 10 && app.drag == Drag::None,
                "Hand dragging did not pan the screenshot.");
        for (Point edge : {Point{100000, -100000}, Point{-100000, 100000}})
        {
            app.view.origin = edge;
            updateView();
            const auto r = navigationRect(), content = imageContentRect();
            require(app.view.toScreen({content.left, content.top}).x <= r.left + .02f &&
                        app.view.toScreen({content.right, content.bottom}).x >= r.right - .02f &&
                        app.view.toScreen({content.left, content.top}).y <= r.top + .02f &&
                        app.view.toScreen({content.right, content.bottom}).y >= r.bottom - .02f,
                    "Panning exposed space past a screenshot edge.");
        }
        zoomAt(center(), 1000);
        require(std::abs(app.view.scale * d - 8) < .001f, "Zoom exceeded or failed to reach 800%.");
        wheel(center() + Point{90, -60}, -12000);
        require(app.fit && std::abs(app.view.scale - minimum) < .0001f,
                "Zooming out did not stop at the fitted scale.");
        requireCentered();
        const auto beforeDrag = app.view.origin;
        SendMessageW(window, WM_MBUTTONDOWN, MK_MBUTTON, mouse(center()));
        SendMessageW(window, WM_MOUSEMOVE, MK_MBUTTON, mouse(center() + Point{100, 50}));
        SendMessageW(window, WM_MBUTTONUP, 0, mouse(center()));
        require(length(app.view.origin - beforeDrag) < .001f,
                "A fitting image could be dragged off center.");
        // Annotation hit targets and resize handles take priority over panning.
        zoomAt(center(), 4);
        Annotation item;
        item.kind = Tool::Rectangle;
        const auto imageCenter = app.view.toImage(center());
        item.a = imageCenter - Point{40, 30};
        item.b = imageCenter + Point{40, 30};
        item.thickness = 3;
        app.document.items = {item};
        app.document.selected = 0;
        const auto handle = app.view.toScreen(item.a);
        require(!handPanAt(handle), "Hand panning stole an annotation resize handle.");
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, mouse(handle));
        require(app.drag == Drag::Resize, "Zoomed annotation resize handle stopped working.");
        finishDrag(true);
        const auto stroke = app.view.toScreen({imageCenter.x, item.a.y});
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, mouse(stroke));
        require(app.drag == Drag::Move, "Hand panning stole an annotation move.");
        finishDrag(true);
        app.document.clear();
        // An axis that fits stays centered even when the other axis can pan.
        app.image = Bitmap::create(3200, 100);
        resetPreview();
        command(Actual);
        app.view.origin.y = -10000;
        updateView();
        require(std::abs(app.view.toScreen({1600, 50}).y - center().y) < .01f,
                "A fitting axis drifted away from the center.");
        app.image = originalPixels;
        resetPreview();
        command(Fit);
    }
    app.dpi = originalDpi;
    saveBytes(L"navigation-test-hand-cursors.png", app.graphics.png(cursorPreview));
    if (app.target)
        app.target->SetDpi(app.dpi * 96, app.dpi * 96);
    SetWindowPos(window, nullptr, originalBounds.left, originalBounds.top,
                 originalBounds.right - originalBounds.left,
                 originalBounds.bottom - originalBounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    command(Fit);
    saveBytes(L"navigation-test-fit.png", app.graphics.png(renderEditorPreview()));
    const float smallScale = app.view.scale;
    command(FullScreen);
    require(app.fit && app.view.scale > smallScale,
            "Full screen did not increase the fitted scale.");
    requireCentered();
    saveBytes(L"navigation-test-full-screen.png", app.graphics.png(renderEditorPreview()));
    command(FullScreen);
    zoomAt(center(), 5);
    const auto focusBeforeResize = app.view.toImage(center());
    SetWindowPos(window, nullptr, 0, 0, originalBounds.right - originalBounds.left + 80,
                 originalBounds.bottom - originalBounds.top + 40,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    updateView();
    require(length(app.view.toImage(center()) - focusBeforeResize) < .1f,
            "Resizing a zoomed view lost the visible image detail.");
    saveBytes(L"navigation-test-zoom.png", app.graphics.png(renderEditorPreview()));
    require(app.document.items.empty() && renderedExport().pixels == originalPixels.pixels,
            "Navigation changed screenshot or export pixels.");
    SetWindowPos(window, nullptr, originalBounds.left, originalBounds.top,
                 originalBounds.right - originalBounds.left,
                 originalBounds.bottom - originalBounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    app.image = originalImage;
    app.document = originalDocument;
    app.exportOptions = originalOptions;
    app.tool = originalTool;
    resetPreview();
    command(Fit);
}
void runResizeTest(HWND window)
{
    using Clock = std::chrono::steady_clock;
    const int samples = app.resizeTestIdle ? 8 : 32;
    auto idle = [&] {
        if (!app.resizeTestIdle)
            return;
        const auto until = GetTickCount64() + 350;
        while (GetTickCount64() < until)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                    throw std::runtime_error("Resize benchmark interrupted.");
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjectsEx(
                0, nullptr, static_cast<DWORD>(until - std::min(until, GetTickCount64())),
                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    };
    std::string report =
        std::string(app.softwareRendering ? "Software" : "Hardware/default") +
        " resize benchmark (milliseconds, " + std::to_string(samples) + " samples per case; " +
        (app.resizeTestIdle ? "350ms message-pumped idle between operations; " : "") +
        "synthetic snip; clipboard untouched)\n";
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = work.right - work.left, height = work.bottom - work.top;
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        releaseImage();
        app.exportOptions = {};
        if (scenario)
        {
            app.image = Bitmap::create(3840, 2160);
            for (size_t i = 0; i < app.image.pixels.size(); i += 4)
            {
                app.image.pixels[i] = 240;
                app.image.pixels[i + 1] = 230;
                app.image.pixels[i + 2] = 220;
                app.image.pixels[i + 3] = 255;
            }
            Annotation item;
            item.kind = Tool::Arrow;
            item.a = {100, 100};
            item.b = {1200, 800};
            app.document.items.push_back(item);
            app.exportOptions.professionalBorder = scenario == 2;
        }
        ShowWindow(window, SW_SHOWNOACTIVATE);
        SetWindowPos(window, nullptr, work.left, work.top, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        repaint();
        UpdateWindow(window); // Warm caches; exclude first-time screenshot composition/upload.
        for (int operation = 0; operation < 3; ++operation)
        {
            ShowWindow(window, SW_RESTORE);
            if (operation == 0)
                SetWindowPos(window, nullptr, 0, 0, std::min(width, 1050), std::min(height, 740),
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            std::vector<double> times;
            Application::PaintTiming sums;
            const auto *cachedPixels = app.previewImage.pixels.data();
            const auto *cachedDisplay = app.displayBitmap.get();
            const auto *cachedPattern = app.workspaceBrush.get();
            for (int i = -4; i < samples; ++i)
            {
                idle();
                const auto paintsBefore = app.resizeTestPaints;
                const bool wasMaximized = IsZoomed(window);
                auto start = Clock::now();
                if (operation == 2)
                    SendMessageW(window, WM_NCLBUTTONDBLCLK, HTCAPTION,
                                 MAKELPARAM(work.left + 100, work.top + 10));
                else if (operation == 1)
                    ShowWindow(window, i % 2 ? SW_RESTORE : SW_MAXIMIZE);
                else
                    SetWindowPos(window, nullptr, 0, 0, i % 2 ? std::min(width, 1050) : width,
                                 i % 2 ? std::min(height, 740) : height,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (app.resizeTestPaints == paintsBefore)
                    throw std::runtime_error(
                        "Resize returned before painting the new editor layout.");
                if (operation == 2 && bool(IsZoomed(window)) == wasMaximized)
                    throw std::runtime_error(
                        "Title-bar double-click did not toggle maximize/restore.");
                if (scenario &&
                    (!app.previewValid || app.previewImage.pixels.data() != cachedPixels))
                    throw std::runtime_error("Resizing unnecessarily recomposed the screenshot.");
                if (app.workspaceBrush.get() != cachedPattern ||
                    (scenario && app.displayBitmap.get() != cachedDisplay))
                    throw std::runtime_error(
                        "Resizing unnecessarily recreated cached drawing resources.");
                if (scenario)
                {
                    const auto canvas = canvasRect();
                    const float padding = previewPadding() * app.view.scale;
                    const auto origin = app.view.origin - Point{padding, padding};
                    const float right = origin.x + app.previewImage.width * app.view.scale;
                    const float bottom = origin.y + app.previewImage.height * app.view.scale;
                    if (origin.x < canvas.left || origin.y < canvas.top || right > canvas.right ||
                        bottom > canvas.bottom ||
                        std::abs(origin.x + right - canvas.left - canvas.right) > .1f ||
                        std::abs(origin.y + bottom - canvas.top - canvas.bottom) > .1f)
                        throw std::runtime_error(
                            "Resized snip was not fitted and centered immediately.");
                }
                double elapsed =
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                if (i >= 0)
                {
                    times.push_back(elapsed);
                    sums.layout += app.paintTiming.layout;
                    sums.background += app.paintTiming.background;
                    sums.content += app.paintTiming.content;
                    sums.present += app.paintTiming.present;
                }
            }
            std::sort(times.begin(), times.end());
            report += std::string(scenario == 0   ? "blank"
                                  : scenario == 1 ? "4K snip"
                                                  : "4K snip + border") +
                      (operation == 2   ? " title-bar double-click"
                       : operation == 1 ? " maximize/restore"
                                        : " resize") +
                      ": median=" + std::to_string(times[samples / 2]) +
                      ", p95=" + std::to_string(times[static_cast<size_t>((samples - 1) * .95)]) +
                      ", worst=" + std::to_string(times.back()) +
                      "; paint averages: layout=" + std::to_string(sums.layout / samples) +
                      ", background=" + std::to_string(sums.background / samples) +
                      ", content=" + std::to_string(sums.content / samples) +
                      ", present=" + std::to_string(sums.present / samples) + "\n";
        }
    }
    writeTestReport(L"resize-test-results.txt", report);
    app.dirty = false;
    DestroyWindow(window);
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
            if (wcscmp(name, L"#32770") == 0 && wcscmp(title, L"Tiger Snip") == 0 &&
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
    static LRESULT CALLBACK observe(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR,
                                    DWORD_PTR data)
    {
        auto &fixture = *reinterpret_cast<SmokeHoverPopup *>(data);
        if (message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE && fixture.popup &&
            IsWindowVisible(fixture.popup))
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
        constexpr wchar_t className[] = L"TigerSnip.SmokeHoverHost.1";
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = app.instance;
        cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        cls.lpszClassName = className;
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throwWindowsError("Cannot register hover-menu test host.");
        host = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, className, L"Hover menu host",
                               WS_OVERLAPPEDWINDOW, x, y, 320, 200, nullptr, nullptr, app.instance,
                               nullptr);
        if (!host)
            throwWindowsError("Cannot create hover-menu test host.");
        if (!SetWindowSubclass(host, observe, 1, reinterpret_cast<DWORD_PTR>(this)))
        {
            DestroyWindow(host);
            throw std::runtime_error("Cannot observe hover-menu focus changes.");
        }
        popup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC",
                                L"Transient hover menu", WS_POPUP | SS_WHITERECT, x + 30, y + 45,
                                220, 100, host, nullptr, app.instance, nullptr);
        if (!popup)
        {
            DestroyWindow(host);
            throwWindowsError("Cannot create hover-menu test popup.");
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
const std::array<Color, 9> PersistenceTestColors = {
    Palette[0],       rgb(12, 34, 56),  rgb(210, 87, 133), rgb(15, 120, 220), rgb(8, 91, 200),
    rgb(90, 35, 170), rgb(18, 90, 140), rgb(25, 50, 75),   rgb(240, 180, 30)};
const std::array<uint8_t, 9> PersistenceTestStyles = {0, 0, 1, 4, 5, 1, 1, 0, 0};
std::wstring smokeMenuPreviewPath;
std::string smokeMenuPreviewError;
void CALLBACK smokeShapeMenuTimer(HWND hwnd, UINT, UINT_PTR id, DWORD)
{
    callbackBoundary<int>(
        [&]() -> int {
            KillTimer(hwnd, id);
            try
            {
                RECT first{}, last{};
                const bool logo = app.shapeMenu == app.logoMenu;
                const bool professional = app.shapeMenu == app.professionalMenu;
                const int count = GetMenuItemCount(app.shapeMenu);
                if (GetMenuItemID(app.shapeMenu, 0) ==
                    static_cast<UINT>(styleCommand(Tool::Check, 0)))
                {
                    if (count != StyleCounts[static_cast<size_t>(Tool::Check)])
                        throw std::runtime_error("Check/X menu is missing styles.");
                    for (int style = 0; style < count; ++style)
                        if (GetMenuItemID(app.shapeMenu, style) !=
                            static_cast<UINT>(styleCommand(Tool::Check, style)))
                            throw std::runtime_error("Check/X menu command or order is incorrect.");
                }
                if (count < 3 || !GetMenuItemRect(hwnd, app.shapeMenu, logo ? 2 : 0, &first) ||
                    !GetMenuItemRect(hwnd, app.shapeMenu, count - 1, &last) ||
                    first.right - first.left < (logo           ? 320
                                                : professional ? 96
                                                               : 184) *
                                                   app.dpi ||
                    first.bottom - first.top < (logo           ? 72
                                                : professional ? 18
                                                               : 48) *
                                                   app.dpi)
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
            return 0;
        },
        [&](const char *failure) {
            OutputDebugStringA(failure);
            EndMenu();
        },
        0);
}
} // namespace

int applicationMain(HINSTANCE instance, int show)
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
    bool selfTest = false, trayOnly = false, snipNow = false, verifyPreferences = false,
         shortcutTest = false, navigationTest = false, paletteTest = false, verifyPalette = false,
         eraserTest = false, penSizeTest = false, verifyPenSize = false, arrowEditTest = false,
         recentTest = false;
    std::filesystem::path testOutputRoot = std::filesystem::current_path() / L"test-output",
                          testFixture;
    for (int i = 1; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"--test-output") == 0 && i + 1 < argc)
            testOutputRoot = std::filesystem::absolute(argv[++i]);
        else if (wcscmp(argv[i], L"--test-fixture") == 0 && i + 1 < argc)
            testFixture = std::filesystem::absolute(argv[++i]);
        else if (wcscmp(argv[i], L"--self-test") == 0)
            selfTest = true;
        else if (wcscmp(argv[i], L"--tray") == 0)
            trayOnly = true;
        else if (wcscmp(argv[i], L"--snip") == 0)
            snipNow = true;
        else if (wcscmp(argv[i], L"--smoke-test") == 0)
            app.smoke = true;
        else if (wcscmp(argv[i], L"--shortcut-test") == 0)
            app.smoke = shortcutTest = true;
        else if (wcscmp(argv[i], L"--navigation-test") == 0)
            app.smoke = navigationTest = true;
        else if (wcscmp(argv[i], L"--palette-test") == 0)
            app.smoke = paletteTest = true;
        else if (wcscmp(argv[i], L"--eraser-test") == 0)
            app.smoke = eraserTest = true;
        else if (wcscmp(argv[i], L"--arrow-edit-test") == 0)
            app.smoke = arrowEditTest = true;
        else if (wcscmp(argv[i], L"--recent-test") == 0)
            app.smoke = recentTest = true;
        else if (wcscmp(argv[i], L"--pen-size-test") == 0)
            penSizeTest = true;
        else if (wcscmp(argv[i], L"--verify-pen-size-preferences") == 0)
            verifyPenSize = true;
        else if (wcscmp(argv[i], L"--verify-palette-preferences") == 0)
            verifyPalette = true;
        else if (wcscmp(argv[i], L"--resize-test") == 0)
            app.resizeTest = true;
        else if (wcscmp(argv[i], L"--resize-idle-test") == 0)
            app.resizeTest = app.resizeTestIdle = true;
        else if (wcscmp(argv[i], L"--software-rendering") == 0)
        {
            app.softwareRendering = true;
            app.rendererSpecified = true;
        }
        else if (wcscmp(argv[i], L"--hardware-rendering") == 0)
        {
            app.softwareRendering = false;
            app.rendererSpecified = true;
        }
        else if (wcscmp(argv[i], L"--diagnostic-instance") == 0)
            app.diagnosticInstance = true;
        else if (wcscmp(argv[i], L"--trace-resize") == 0)
        {
            wchar_t temporary[MAX_PATH]{};
            if (GetTempPathW(MAX_PATH, temporary))
                app.resizeTrace.open(
                    std::filesystem::path(temporary) /
                    (L"Tiger Snip-resize-" + std::to_wstring(GetCurrentProcessId()) + L".log"));
        }
        else if (wcscmp(argv[i], L"--verify-smoke-preferences") == 0)
            verifyPreferences = true;
    }
    LocalFree(argv);
    int result = 0;
    HANDLE mutex = nullptr;
    try
    {
        if (selfTest || app.smoke || verifyPreferences || verifyPalette || penSizeTest ||
            verifyPenSize || app.resizeTest)
        {
            testReportDirectory = createTestDirectory(testOutputRoot);
            if ((verifyPreferences || verifyPalette || verifyPenSize) && testFixture.empty())
                throw std::runtime_error("Restart verification requires --test-fixture with the "
                                         "producer run directory.");
            std::filesystem::current_path(testFixture.empty() ? testReportDirectory : testFixture);
        }
        if (penSizeTest || verifyPenSize)
        {
            app.iniPath = (std::filesystem::current_path() / L"pen-size-settings.ini").wstring();
            if (penSizeTest)
            {
                testPenSizePreferences();
                writeTestReport(L"pen-size-test-results.txt",
                                "PASS: default pen size, size-only preference write, reload, "
                                "1-100px bounds, no-op writes, "
                                "selected-stroke resizing with undo, independent highlight/text "
                                "sizes, saved restart fixture.\n");
            }
            else
            {
                loadToolPreferences();
                if (app.thickness != 13 || app.highlightWidth != 31 || app.fontSize != 35 ||
                    app.colors[static_cast<size_t>(Tool::Pen)] != rgb(12, 34, 56) ||
                    app.toolPreferencesDirty || app.tool != Tool::Select)
                    throw std::runtime_error("Pen size/color or independent highlight/text sizes "
                                             "did not survive a process exit.");
                writeTestReport(L"pen-size-preference-results.txt",
                                "PASS: a fresh process restored 13px pen size and its color, "
                                "independent highlight/text sizes, "
                                "no pending preference write and unchanged active tool.\n");
            }
        }
        else if (verifyPalette)
        {
            app.iniPath = (std::filesystem::current_path() / L"smoke-settings.ini").wstring();
            loadToolPreferences();
            if (app.palette != PersistenceTestPalette || app.paletteDirty)
                throw std::runtime_error(
                    "Saved palette count, order and colors did not survive a process exit.");
            writeTestReport(L"palette-preference-results.txt",
                            "PASS: a fresh process restored all ten saved palette colors in order, "
                            "with no pending write.\n");
        }
        else if (verifyPreferences)
        {
            wchar_t testDirectory[32768]{};
            GetCurrentDirectoryW(32768, testDirectory);
            app.iniPath = std::wstring(testDirectory) + L"\\smoke-settings.ini";
            loadToolPreferences();
            if (preferenceUInt(app.iniPath, L"Settings", L"Hotkey", 0) !=
                    MAKEWORD(VK_F20, HOTKEYF_CONTROL | HOTKEYF_ALT) ||
                preferenceUInt(app.iniPath, L"Settings", L"InstantHotkey", 0) != VK_PAUSE)
                throw std::runtime_error(
                    "Independent capture shortcuts did not survive a process exit.");
            if (app.colors != PersistenceTestColors || app.styles != PersistenceTestStyles ||
                app.palette != PersistenceTestPalette || app.paletteDirty ||
                app.tool != Tool::Select || app.toolPreferencesDirty || app.fontSize != 40 ||
                !app.textBold || !app.textBox || app.geometryTool != Tool::Rectangle ||
                !app.exportOptions.professionalBorder || !app.exportOptions.samtecLogo ||
                app.exportPreferencesDirty || app.exportOptions.professionalBlur ||
                !app.exportOptions.professionalRounded ||
                app.saveFolder !=
                    (std::filesystem::path(testDirectory) / L"smoke-save location").wstring() ||
                app.collapsedRows != 6 || app.layoutPreferencesDirty || app.fullScreen ||
                app.exportOptions.samtecStyle != 5)
                throw std::runtime_error(
                    "Tool preferences did not survive a complete process exit.");
            writeTestReport(
                L"preference-test-results.txt",
                "PASS: a fresh process restored every tool's style and custom color after full "
                "exit; "
                "Professional Border with separate blur/rounding, Samtec Logo and selected style, "
                "save location, collapsed rows, and both capture shortcuts restored, active tool "
                "unchanged, no pending preference write.\n");
        }
        else if (selfTest)
        {
            app.graphics.test();
            testPenCursor();
            testChiselCursor();
            writeTestReport(
                L"self-test-results.txt",
                "PASS: model history, cancellation, hit testing, resizing, coordinate "
                "transforms, cropping, pen/circle/arrow/check composition, translucent chisel "
                "highlights with uniform "
                "stroke overlap, readable text, nib geometry, hit testing/resizing, and PNG round "
                "trips, six check/X styles at small and large sizes, "
                "solid/dashed/dotted line patterns, outlined/curved/straight/block gloss arrow "
                "artwork and "
                "selection, PNG "
                "pixel-perfect round trip, moved annotations, image color sampling, "
                "text/bold/multiline/rounded-box export, square/rounded/highlight/filled "
                "rectangles, "
                "native pen cursor color/size/hotspot and chisel cursor alpha composition without "
                "white fringes on light/dark backgrounds at 100/150/200% DPI and 10/100/800% zoom; "
                "Professional Border default OFF, independent blur/rounding, 20px transparent "
                "padding with blur, softened neutral halo on white/black without a hard outline, "
                "unchanged content, tiny snips, transparent PNG round trip.\n");
        }
        else
        {
            const std::wstring mutexName =
                app.diagnosticInstance
                    ? L"Local\\TigerSnip.ResizeDiagnostic." + std::to_wstring(GetCurrentProcessId())
                    : L"Local\\TigerSnip.SingleInstance.1";
            mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
            if (!mutex)
                throw std::runtime_error("Cannot initialize the app instance.");
            if (GetLastError() == ERROR_ALREADY_EXISTS && !app.smoke && !app.resizeTest)
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
            if (!InitCommonControlsEx(&controls))
                throwWindowsError("Cannot initialize Windows controls.");
            app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
            registerClasses();
            auto path = executablePath();
            app.iniPath = path.substr(0, path.find_last_of(L"\\/") + 1) + L"TigerSnip.ini";
            if (!app.smoke && !app.resizeTest && !app.diagnosticInstance)
                app.iniPath = personalSettingsPath();
            if (app.smoke)
            {
                if (app.exportOptions.professionalBorder || app.exportOptions.samtecLogo ||
                    app.exportPreferencesDirty)
                    throw std::runtime_error(
                        "Professional Border must start OFF without saved preferences.");
                wchar_t testDirectory[32768]{};
                GetCurrentDirectoryW(32768, testDirectory);
                app.iniPath = std::wstring(testDirectory) + L"\\smoke-settings.ini";
            }
            else if (!app.resizeTest)
                loadToolPreferences();
            if (app.resizeTrace.is_open())
            {
                app.resizeTrace << "Tiger Snip resize trace; pid=" << GetCurrentProcessId()
                                << "; build=" << __DATE__ << " " << __TIME__ << "; renderer="
                                << (app.softwareRendering ? "software" : "hardware/default")
                                << "\n";
                app.resizeTrace.flush();
            }
            if (app.diagnosticInstance)
            {
                wchar_t temporary[MAX_PATH]{};
                if (!GetTempPathW(MAX_PATH, temporary))
                    throwWindowsError("Cannot create isolated diagnostic settings.");
                app.iniPath =
                    (std::filesystem::path(temporary) /
                     (L"Tiger Snip-diagnostic-" + std::to_wstring(GetCurrentProcessId()) + L".ini"))
                        .wstring();
            }
            app.hotkey =
                static_cast<WORD>(preferenceUInt(app.iniPath, L"Settings", L"Hotkey", app.hotkey));
            app.instantHotkey = static_cast<WORD>(
                preferenceUInt(app.iniPath, L"Settings", L"InstantHotkey", app.instantHotkey));
            HDC dc = GetDC(nullptr);
            if (!dc)
                throwWindowsError("Cannot read the desktop display settings.");
            float dpi = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
            ReleaseDC(nullptr, dc);
            app.dialogFont =
                CreateFontW(-static_cast<int>(14 * dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            if (!app.dialogFont)
                throwWindowsError("Cannot create the dialog font.");
            RECT work{};
            if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
                throwWindowsError("Cannot locate the desktop work area.");
            int width = std::min(static_cast<int>(1050 * dpi),
                                 static_cast<int>(work.right - work.left)),
                height =
                    std::min(static_cast<int>(740 * dpi), static_cast<int>(work.bottom - work.top));
            HWND window = CreateWindowExW(
                0, app.diagnosticInstance ? DiagnosticClass : MainClass,
                app.diagnosticInstance ? L"Tiger Snip - Resize diagnostic" : L"Tiger Snip",
                WS_OVERLAPPEDWINDOW, work.left + (work.right - work.left - width) / 2,
                work.top + (work.bottom - work.top - height) / 2, width, height, nullptr,
                createMenu(), instance, nullptr);
            if (!window)
                throwWindowsError("Cannot create the editor window.");
            if (app.resizeTrace.is_open())
                SetTimer(window, TraceHeartbeatTimer, 250, nullptr);
            // The smoke process must not compete with the user's running global shortcut.
            WORD initial = app.smoke || app.resizeTest || app.diagnosticInstance ? 0 : app.hotkey;
            WORD initialFull =
                app.smoke || app.resizeTest || app.diagnosticInstance ? 0 : app.instantHotkey;
            app.hotkey = 0;
            app.instantHotkey = 0;
            if (!registerShortcuts(initial, initialFull, false))
            {
                // An unavailable new default must not disable the user's existing area shortcut.
                if (registerShortcuts(initial, 0, false))
                    status(L"Instant shortcut unavailable - choose another in Settings");
                else if (registerShortcuts(0, initialFull, false))
                    status(L"Area shortcut unavailable - choose another in Settings");
                else
                    status(L"Shortcuts unavailable - change them in Settings");
            }
            if (!trayOnly || !app.tray)
            {
                ShowWindow(window, app.resizeTest ? SW_SHOWNOACTIVATE : show);
                UpdateWindow(window);
            }
            if (snipNow)
                startSnip();
            SmokeNoPromptGuard noPrompts(app.smoke);
            if (app.resizeTest)
                runResizeTest(window);
            else if (navigationTest)
            {
                testNavigation(window);
                writeTestReport(L"navigation-test-results.txt",
                                "PASS: ordinary wheel zoom, dynamic fitted minimum, pointer "
                                "anchoring, 800% maximum, "
                                "open/closed hand cursors, left-drag panning, bounded edges, "
                                "centered fitting axes, "
                                "annotation move/resize priority at 100/150/200% DPI, "
                                "resize/full-screen fit and focus, unchanged export.\n");
                command(Exit);
            }
            else if (paletteTest)
            {
                testPaletteTools();
                app.palette = PersistenceTestPalette;
                app.paletteDirty = true;
                writeTestReport(L"palette-test-results.txt",
                                "PASS: picker spectrum/hue, RGB/hex validation, Add/Edit/Cancel, "
                                "duplicates, selected annotation "
                                "recolor and undo/export, uninterrupted live text editing, "
                                "deleting without changing annotations, 0/4/10/18 saved colors, "
                                "wrapped swatch mouse clicks and toolbar layout at 100/150/200% "
                                "DPI, malformed preferences.\n");
                command(Exit);
            }
            else if (eraserTest)
            {
                testEraserTool();
                writeTestReport(L"eraser-test-results.txt",
                                "PASS: eraser toolbar/E toggle, older whole stroke deletion while "
                                "keeping newer annotations, "
                                "topmost overlapping object per click, sparse pointer sweeps, all "
                                "annotation types/styles, "
                                "one undo step per gesture, redo, Esc/capture-loss cancellation, "
                                "unchanged screenshot/export restoration, "
                                "no-op redo preservation, cached cursor/hits at 100/150/200% DPI "
                                "and 50/100/800% zoom, toolbar layout.\n");
                command(Exit);
            }
            else if (recentTest)
            {
                testRecentSnips();
                acceptCapture(Bitmap::create(10, 10));
                command(Exit);
                if (!app.recent.empty())
                    throw std::runtime_error(
                        "Exiting did not clear the recent capture collection.");
                writeTestReport(L"recent-test-results.txt",
                                "PASS: last 10 session captures including current, oldest eviction "
                                "and stable order, active-buffer ownership, "
                                "text/crop/undo/redo/zoom/save-path restoration, PNG export, "
                                "canceled capture, close-to-tray retention, "
                                "mouse and keyboard selection/dismissal, compact scroll, popup "
                                "layout at 100/150/200% DPI, "
                                "collapsed rows and full screen, cached thumbnails, switch/paint "
                                "timing, empty fresh session and Exit cleanup.\n");
            }
            else if (arrowEditTest)
            {
                testCurvedArrowControls();
                writeTestReport(L"arrow-edit-test-results.txt",
                                "PASS: curved arrow Flip mouse control at 100/150/200% DPI and "
                                "50/100/800% zoom; "
                                "selected object only, edge positioning, unchanged tool "
                                "preferences, one-step undo/redo, "
                                "hit testing, preview/export agreement and PNG round trips.\n");
                command(Exit);
            }
            else if (shortcutTest)
            {
                testShortcutFields();
                writeTestReport(L"shortcut-test-results.txt",
                                "PASS: visible shortcut labels, restored binding, Home/Pause key "
                                "entry, Backspace, Pause key, "
                                "Cancel, Save, real Windows hotkey delivery, instant capture and "
                                "reopened settings.\n");
                command(Exit);
            }
            else if (app.smoke)
            {
                // Exercise real Win32, Direct2D, and capture initialization without changing the
                // clipboard.
                ShowWindow(window, SW_SHOWNOACTIVATE);
                UpdateWindow(window);
                DwmFlush();
                saveBytes(L"smoke-test-home.png", app.graphics.png(renderEditorPreview()));
                testRenderingSettings();
                testHighlightTool();
                testPaletteTools();
                testEraserTool();
                testCurvedArrowControls();
                testRecentSnips();
                testCropTool();
                testCaptureShortcuts();
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
                    throwWindowsError("Cannot create capture regression backing.");
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
                    throw std::runtime_error(
                        "New snip prompted instead of immediately hiding the unsaved editor.");
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
                const auto instantBacking = app.desktop.crop(
                    editorBounds.left - app.virtualX, editorBounds.top - app.virtualY,
                    backingPixels.width, backingPixels.height);
                if (instantBacking.pixels != backingPixels.pixels)
                    throw std::runtime_error(
                        "Visible editor or its fade leaked into instant hotkey capture.");
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
                                                          popup.bounds.right - popup.bounds.left,
                                                          popup.bounds.bottom - popup.bounds.top);
                    SendMessageW(window, WM_HOTKEY, app.hotkeyId, 0);
                    if (!popup.dismissed)
                        throw std::runtime_error(
                            "Hover-menu fixture did not dismiss on focus loss.");
                    {
                        if (SmokeNoPromptGuard::shown || !app.overlay || app.capturePending)
                            throw std::runtime_error(
                                "Hotkey capture still waits for a capture timer.");
                        const HWND firstOverlay = app.overlay;
                        SendMessageW(window, WM_HOTKEY, app.hotkeyId, 0);
                        if (app.overlay != firstOverlay)
                            throw std::runtime_error(
                                "Repeated shortcut replaced active selection.");
                        const auto frozen = app.desktop.crop(popup.bounds.left - app.virtualX,
                                                             popup.bounds.top - app.virtualY,
                                                             reference.width, reference.height);
                        if (frozen.pixels != reference.pixels)
                            throw std::runtime_error(
                                "Instant capture lost the hover popup before freezing the screen.");
                        SendMessageW(app.overlay, WM_KEYDOWN, VK_ESCAPE, 0);
                        if (app.overlay || !app.desktop.empty() || !IsWindowVisible(window) ||
                            app.image.width != 180 || app.image.height != 120)
                            throw std::runtime_error(
                                "Canceling instant selection failed to restore the previous snip.");
                        if ((mode & 1) && (!app.dirty || app.document.items.size() != 1))
                            throw std::runtime_error(
                                "Canceling selection lost previous unsaved annotations.");
                    }
                    SendMessageW(window, WM_TIMER, CaptureTimer, 0);
                    if (app.overlay || app.capturePending || !app.desktop.empty())
                        throw std::runtime_error(
                            "A stale capture timer reopened canceled instant selection.");
                }
                POINT previousPointer{};
                GetCursorPos(&previousPointer);
                int cursorVisibilityCalls = 1;
                while (ShowCursor(TRUE) < 0)
                    ++cursorVisibilityCalls;
                for (int mode = 0; mode < 4; ++mode)
                {
                    releaseImage();
                    app.image = Bitmap::create(180, 120);
                    app.dirty = (mode & 1) != 0;
                    if (app.dirty)
                        app.document.items.push_back(unsaved);
                    showEditor();
                    if (mode >= 2)
                        ShowWindow(window, SW_HIDE);
                    SmokeHoverPopup popup(editorBounds.left + 40, editorBounds.top + 100);
                    SetCursorPos(popup.bounds.left + 30, popup.bounds.top + 30);
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                    const auto reference = captureDesktop(
                        popup.bounds.left, popup.bounds.top, popup.bounds.right - popup.bounds.left,
                        popup.bounds.bottom - popup.bounds.top, true);
                    const auto withoutPointer = captureDesktop(popup.bounds.left, popup.bounds.top,
                                                               reference.width, reference.height);
                    if (reference.pixels == withoutPointer.pixels)
                    {
                        CURSORINFO pointer{};
                        pointer.cbSize = sizeof(pointer);
                        GetCursorInfo(&pointer);
                        throw std::runtime_error(
                            "Instant capture fixture did not contain the pointer: flags=" +
                            std::to_string(pointer.flags) +
                            " position=" + std::to_string(pointer.ptScreenPos.x) + "," +
                            std::to_string(pointer.ptScreenPos.y) +
                            " expected=" + std::to_string(popup.bounds.left + 30) + "," +
                            std::to_string(popup.bounds.top + 30));
                    }
                    SendMessageW(window, WM_HOTKEY, app.instantHotkeyId, 0);
                    if (!popup.dismissed || app.overlay || app.capturePending ||
                        !app.desktop.empty() ||
                        app.image.width != GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
                        app.image.height != GetSystemMetrics(SM_CYVIRTUALSCREEN) ||
                        !IsWindowVisible(window) || app.tool != Tool::Pen ||
                        !app.document.items.empty())
                        throw std::runtime_error("Instant all-monitor shortcut did not open the "
                                                 "frozen desktop in the editor.");
                    const auto capturedPopup = app.image.crop(popup.bounds.left - app.virtualX,
                                                              popup.bounds.top - app.virtualY,
                                                              reference.width, reference.height);
                    if (capturedPopup.pixels != reference.pixels)
                    {
                        saveBytes(L"smoke-test-pointer-reference.png", app.graphics.png(reference));
                        saveBytes(L"smoke-test-pointer-captured.png",
                                  app.graphics.png(capturedPopup));
                        throw std::runtime_error(
                            "Instant all-monitor capture lost the hover menu or pointer.");
                    }
                    SendMessageW(window, WM_TIMER, CaptureTimer, 0);
                    if (app.overlay || app.capturePending)
                        throw std::runtime_error(
                            "Stale timer opened selection after instant all-monitor capture.");
                }
                SetCursorPos(previousPointer.x, previousPointer.y);
                while (cursorVisibilityCalls-- > 0)
                    ShowCursor(FALSE);
                releaseImage();
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
                        app.thickness = 99;
                    else
                        app.fontSize = 143;
                    auto point = startSizeHold(SizeUp);
                    sizeTicks(4);
                    endSizeHold(point);
                    if (sizeValue() != (tool == Tool::Pen ? 100 : 144))
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
                if (!app.pickingColor || activeColor() != penColor ||
                    app.document.items.size() != 1)
                    throw std::runtime_error(
                        "Eyedropper sampled workspace pixels or drew an annotation.");
                processKey(VK_ESCAPE);
                if (app.pickingColor || app.tool != Tool::Pen || activeColor() != penColor)
                    throw std::runtime_error(
                        "Canceling the eyedropper changed the previous tool/color.");
                for (float scale : {1.0f, 2.0f, 4.0f})
                {
                    app.fit = false;
                    app.view.scale = scale / app.dpi;
                    const auto r = navigationRect();
                    app.view.origin = Point{(r.left + r.right) / 2, (r.top + r.bottom) / 2} -
                                      Point{320, 180} * app.view.scale;
                    updateView();
                    processKey('I');
                    const Point sample{320, 180};
                    const Point screen = app.view.toScreen(sample);
                    const LPARAM click = MAKELPARAM(static_cast<int>(screen.x * app.dpi),
                                                    static_cast<int>(screen.y * app.dpi));
                    const Point actual = app.view.toImage(
                        {GET_X_LPARAM(click) / app.dpi, GET_Y_LPARAM(click) / app.dpi});
                    const auto expected = app.image.sample(actual);
                    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, click);
                    SendMessageW(window, WM_LBUTTONUP, 0, click);
                    if (!expected || app.pickingColor || app.tool != Tool::Pen ||
                        activeColor() != *expected || app.document.items.size() != 1 ||
                        app.drag != Drag::None || GetCapture() == window)
                        throw std::runtime_error(
                            "Eyedropper sampling at different zoom levels failed: scale=" +
                            std::to_string(scale) + " picking=" + std::to_string(app.pickingColor) +
                            " x=" + std::to_string(screen.x) + " y=" + std::to_string(screen.y));
                    const auto cursor = editorCursor(screen);
                    if (cursor != app.penCursor || app.penCursorColor != *expected ||
                        std::abs(app.penCursorDiameter -
                                 std::max(1.0f, app.thickness * app.view.scale * app.dpi)) > .001f)
                        throw std::runtime_error(
                            "Picked color or zoom did not update the pen cursor.");
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
                    throw std::runtime_error(
                        "Eyedropper did not sample annotations/recolor the selection.");
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
                    dragImage({bounds.right, bounds.bottom},
                              {bounds.right + 20, bounds.bottom + 20});
                    bounds = app.document.items.back().bounds();
                    dragImage({(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2},
                              {(bounds.left + bounds.right) / 2 - 30,
                               (bounds.top + bounds.bottom) / 2 - 20});
                    const auto &cross = app.document.items.back();
                    if (cross.kind != Tool::Check || cross.style != style ||
                        cross.color != Palette[0] || cross.bounds().width() < 70 ||
                        app.tool != Tool::Select ||
                        !cross.hit({(cross.a.x + cross.b.x) / 2, (cross.a.y + cross.b.y) / 2}, 1))
                        throw std::runtime_error(
                            "Red X click placement, move, resize, or selection failed.");
                    command(DeleteSelected);
                    command(Undo);
                    if (app.document.items.back().style != style)
                        throw std::runtime_error("Undo red X deletion lost its style.");
                    command(Redo);
                    if (app.document.items.size() != 4)
                        throw std::runtime_error("Redo red X deletion failed.");
                    command(styleCommand(Tool::Check, style));
                    dragImage({450, 20}, {520, 90});
                    if (app.document.items.back().style != style ||
                        app.document.items.back().color != Palette[0] ||
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
                        throw std::runtime_error("Outlined, curved, straight, or block gloss arrow "
                                                 "placement/selection failed.");
                }
                const size_t beforeText = app.document.items.size();
                clickButton(TextTool);
                dragImage({20, 200}, {20, 200});
                SendMessageW(app.textEdit, WM_CHAR, 'X', 0);
                dragImage({600, 40}, {600, 40});
                if (app.textEdit || app.document.editing() || app.tool != Tool::Select ||
                    app.document.items.size() != beforeText + 1 ||
                    app.document.items.back().text != L"X")
                    throw std::runtime_error(
                        "Clicking outside typed text did not commit and return to Select.");
                command(Undo);
                if (app.document.items.size() != beforeText)
                    throw std::runtime_error(
                        "Click-away text creation was not a single undoable edit.");
                clickButton(TextTool);
                dragImage({250, 270}, {250, 270});
                if (!app.textEdit || GetFocus() != app.textEdit || !app.document.editing())
                    throw std::runtime_error(
                        "Click-to-type did not open and focus inline text editing.");
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
                if (emptyField.right > 32 ||
                    (GetWindowLongPtrW(app.textEdit, GWL_STYLE) & WS_BORDER))
                    throw std::runtime_error(
                        "Empty inline text still shows a wide bordered input bar.");
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
                    HBITMAP bitmap =
                        CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
                    HDC dc = CreateCompatibleDC(nullptr);
                    if (!bitmap || !bits || !dc)
                    {
                        if (bitmap)
                            DeleteObject(bitmap);
                        if (dc)
                            DeleteDC(dc);
                        throw std::runtime_error("Cannot capture native inline editor.");
                    }
                    HGDIOBJ previous = SelectObject(dc, bitmap);
                    SendMessageW(app.textEdit, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc),
                                 PRF_CLIENT | PRF_ERASEBKGND);
                    GdiFlush();
                    std::copy_n(static_cast<const uint8_t *>(bits), native.pixels.size(),
                                native.pixels.begin());
                    SelectObject(dc, previous);
                    DeleteDC(dc);
                    DeleteObject(bitmap);
                    const auto actual =
                        native.sample({float(native.width - 1), float(native.height - 1)});
                    const auto expected = scene.sample(
                        {float(origin.x + native.width - 1), float(origin.y + native.height - 1)});
                    if (actual != expected)
                    {
                        for (size_t i = 3; i < native.pixels.size(); i += 4)
                            native.pixels[i] = 255;
                        saveBytes(L"smoke-test-inline-failure.png", app.graphics.png(native));
                        throw std::runtime_error("Inline editor background mismatch: actual=" +
                                                 std::to_string(actual.value_or(0)) + " expected=" +
                                                 std::to_string(expected.value_or(0)));
                    }
                    for (int y = 0; y < native.height; ++y)
                        for (int x = 0; x < native.width; ++x)
                            if (origin.x + x >= 0 && origin.x + x < scene.width &&
                                origin.y + y >= 0 && origin.y + y < scene.height)
                            {
                                const size_t src = (static_cast<size_t>(y) * native.width + x) * 4;
                                const size_t dst =
                                    (static_cast<size_t>(origin.y + y) * scene.width + origin.x +
                                     x) *
                                    4;
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
                    throw std::runtime_error(
                        "Inline editor did not grow with its text or wrapped prematurely.");
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
                    app.document.items.back().fontSize != 32 ||
                    app.document.items.back().color != Palette[4])
                    throw std::runtime_error(
                        "Inline text typing/font/bold/box/color controls failed.");
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
                if (!boldShortcut || app.textEdit || app.document.editing() ||
                    app.tool != Tool::Select || app.document.items.size() != beforeText + 1)
                    throw std::runtime_error("Ctrl+Enter did not commit the text annotation.");
                command(Undo);
                if (app.document.items.size() != beforeText)
                    throw std::runtime_error("Text creation was not one undoable edit.");
                command(Redo);
                auto doubleClickText = [&] {
                    const auto point =
                        app.view.toScreen(app.document.items.back().a + Point{15, 15});
                    SendMessageW(window, WM_LBUTTONDBLCLK, MK_LBUTTON,
                                 MAKELPARAM(static_cast<int>(point.x * app.dpi),
                                            static_cast<int>(point.y * app.dpi)));
                    if (!app.textEdit)
                        throw std::runtime_error("Double-click did not reopen text editing.");
                };
                doubleClickText();
                SetWindowTextW(app.textEdit, L"Canceled changes");
                SendMessageW(app.textEdit, WM_KEYDOWN, VK_ESCAPE, 0);
                if (app.textEdit || app.document.items.back().text != L"Review this value")
                    throw std::runtime_error(
                        "Canceling text editing failed to restore the original.");
                doubleClickText();
                SetWindowTextW(app.textEdit, L"Review this value\r\nBefore sharing");
                updateTextFromEditor();
                RECT multilineField{};
                GetClientRect(app.textEdit, &multilineField);
                if (SendMessageW(app.textEdit, EM_GETLINECOUNT, 0, 0) != 2 ||
                    multilineField.bottom <= shortField.bottom)
                    throw std::runtime_error(
                        "Inline editor did not grow vertically for a new line.");
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
                    throw std::runtime_error(
                        "Canceling empty text left an annotation or pending edit.");
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
                    throw std::runtime_error(
                        "Combined shape button did not remember its rectangle choice.");
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
                    if (app.exportOptions.professionalBorder != border ||
                        !app.exportPreferencesDirty ||
                        bool(GetMenuState(GetMenu(window), ProfessionalBorder, MF_BYCOMMAND) &
                             MF_CHECKED) != border)
                        throw std::runtime_error(
                            "Professional Border Settings toggle or checkmark failed.");
                    const auto exported = renderedExport();
                    if (exported.width != app.image.width + (border ? 40 : 0) ||
                        exported.height != app.image.height + (border ? 40 : 0) ||
                        (!border && exported.pixels != flattened.pixels))
                        throw std::runtime_error(
                            "Shared export pipeline did not honor Professional Border.");
                    app.savePath = border ? L"smoke-test-professional-border.png"
                                          : L"smoke-test-border-off.png";
                    command(Save);
                    std::ifstream saved(std::filesystem::path(app.savePath), std::ios::binary);
                    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(saved)), {});
                    const auto decoded = app.graphics.decode(bytes);
                    if (decoded.width != exported.width || decoded.height != exported.height ||
                        decoded.pixels != exported.pixels || app.dirty)
                        throw std::runtime_error(
                            "Save PNG did not bake in the selected border effect.");
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
                        bool(GetMenuState(GetMenu(window), SamtecLogo, MF_BYCOMMAND) &
                             MF_CHECKED) != logo)
                        throw std::runtime_error(
                            "Samtec Logo Settings toggle or checkmark failed.");
                    const auto exported = renderedExport();
                    if (exported.width != app.image.width + 40 ||
                        exported.height != app.image.height + 40)
                        throw std::runtime_error(
                            "Samtec Logo changed exported screenshot dimensions.");
                    app.savePath =
                        logo ? L"smoke-test-samtec-logo.png" : L"smoke-test-samtec-off.png";
                    command(Save);
                    std::ifstream saved(std::filesystem::path(app.savePath), std::ios::binary);
                    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(saved)), {});
                    if (app.graphics.decode(bytes).pixels != exported.pixels)
                        throw std::runtime_error("PNG Save did not honor the Samtec Logo setting.");
                }
                command(SamtecLogo); // Verify the logo setting survives close and full exit.
                if (GetMenuItemCount(app.logoMenu) != 8)
                    throw std::runtime_error(
                        "Samtec Logo submenu must contain Enabled and six styles.");
                for (int style = 0; style < 6; ++style)
                {
                    command(LogoStyleFirst + style);
                    updateMenus();
                    if (!app.exportOptions.samtecLogo || app.exportOptions.samtecStyle != style ||
                        !(GetMenuState(app.logoMenu, LogoStyleFirst + style, MF_BYCOMMAND) &
                          MF_CHECKED))
                        throw std::runtime_error(
                            "Samtec Logo style selection or radio indicator failed.");
                    const auto exported = renderedExport();
                    app.savePath =
                        L"smoke-test-samtec-style-" + std::to_wstring(style + 1) + L".png";
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
                TrackPopupMenu(app.logoMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, menuAnchor.x,
                               menuAnchor.y, 0, window, nullptr);
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
                    updateView();
                    const float inset = previewPadding() * app.view.scale;
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
                            // Centering can place native-size pixels between device pixels.
                            // Compare the source texel under the destination pixel's center.
                            const int tx = static_cast<int>(
                                std::floor(((sx + .5f) / app.dpi - app.view.origin.x + inset) /
                                           app.view.scale));
                            const int ty = static_cast<int>(
                                std::floor(((sy + .5f) / app.dpi - app.view.origin.y + inset) /
                                           app.view.scale));
                            if (tx < 0 || ty < 0 || tx >= styled.width || ty >= styled.height)
                                continue;
                            const size_t src = (static_cast<size_t>(ty) * styled.width + tx) * 4;
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
                    throw std::runtime_error(
                        "Copy flash was invisible or changed exported pixels.");
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
                            (b.rect.bottom > toolbarHeight()))
                            throw std::runtime_error("Compact toolbar control is clipped.");
                    saveBytes(L"smoke-test-layout-" +
                                  std::to_wstring(static_cast<int>(scale * 100)) + L".png",
                              app.graphics.png(renderEditorPreview()));
                    command(TextTool);
                    buildButtons();
                    for (const auto &b : app.buttons)
                        if (b.rect.right > bounds.right || (b.rect.bottom > toolbarHeight()))
                            throw std::runtime_error(
                                "Text formatting control is clipped at high DPI.");
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
                testNavigation(window);
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
                            if ((b.rect.left < 0 || b.rect.right > clientDips().right ||
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
                if (!app.hotkeyControl || !app.instantHotkeyControl ||
                    static_cast<WORD>(SendMessageW(app.hotkeyControl, HKM_GETHOTKEY, 0, 0)) !=
                        app.hotkey ||
                    static_cast<WORD>(SendMessageW(app.instantHotkeyControl, HKM_GETHOTKEY, 0,
                                                   0)) != app.instantHotkey)
                    throw std::runtime_error("Shortcut settings initialization failed.");
                saveBytes(L"smoke-test-shortcuts.png",
                          app.graphics.png(renderNativeWindow(app.settingsWindow)));
                SendMessageW(app.instantHotkeyControl, HKM_SETHOTKEY,
                             MAKEWORD(VK_F21, HOTKEYF_CONTROL | HOTKEYF_ALT), 0);
                SendMessageW(app.hotkeyControl, HKM_SETHOTKEY,
                             MAKEWORD(VK_F20, HOTKEYF_CONTROL | HOTKEYF_ALT), 0);
                closeSettings();
                if (app.hotkey || app.instantHotkey)
                    throw std::runtime_error("Canceling shortcut settings changed the bindings.");
                openSettings();
                // Exercise actual key entry, rather than just assigning a hotkey value.
                SendMessageW(app.instantHotkeyControl, HKM_SETHOTKEY, 0, 0);
                const LPARAM pauseScan =
                    static_cast<LPARAM>(MapVirtualKeyW(VK_PAUSE, MAPVK_VK_TO_VSC)) << 16;
                SendMessageW(app.instantHotkeyControl, WM_KEYDOWN, VK_PAUSE, pauseScan | 1);
                SendMessageW(app.instantHotkeyControl, WM_KEYUP, VK_PAUSE,
                             pauseScan | 0xc0000001LL);
                if (SendMessageW(app.instantHotkeyControl, HKM_GETHOTKEY, 0, 0) != VK_PAUSE)
                    throw std::runtime_error(
                        "Shortcut field did not accept Pause without modifiers.");
                SendMessageW(app.instantHotkeyControl, HKM_SETHOTKEY, 0, 0);
                SendMessageW(app.instantHotkeyControl, WM_KEYDOWN, VK_PAUSE, (0x45 << 16) | 1);
                if (SendMessageW(app.instantHotkeyControl, HKM_GETHOTKEY, 0, 0) != VK_PAUSE)
                    throw std::runtime_error("Pause key did not assign the instant capture key.");
                wchar_t pauseLabel[64]{};
                GetWindowTextW(app.instantHotkeyControl, pauseLabel, 64);
                if (std::wstring(pauseLabel) != L"Pause")
                    throw std::runtime_error(
                        "Pause shortcut value was stored without a visible label.");
                saveBytes(L"smoke-test-pause-shortcut.png",
                          app.graphics.png(renderNativeWindow(app.settingsWindow)));
                SendMessageW(app.hotkeyControl, HKM_SETHOTKEY,
                             MAKEWORD(VK_F20, HOTKEYF_CONTROL | HOTKEYF_ALT), 0);
                auto originalIni = app.iniPath;
                wchar_t testDirectory[32768]{};
                GetCurrentDirectoryW(32768, testDirectory);
                app.iniPath = std::wstring(testDirectory) + L"\\smoke-settings.ini";
                SendMessageW(app.settingsWindow, WM_COMMAND, IDOK, 0);
                if (app.settingsWindow ||
                    static_cast<WORD>(preferenceUInt(app.iniPath, L"Settings", L"Hotkey", 0)) !=
                        app.hotkey ||
                    static_cast<WORD>(preferenceUInt(app.iniPath, L"Settings", L"InstantHotkey",
                                                     0)) != app.instantHotkey)
                    throw std::runtime_error("Shortcut settings save failed.");
                // Use the Windows input queue and real RegisterHotKey delivery, rather than
                // sending a WM_HOTKEY directly to the app.
                MSG hotkeyMessage{};
                while (PeekMessageW(&hotkeyMessage, window, WM_HOTKEY, WM_HOTKEY, PM_REMOVE))
                {
                }
                INPUT pauseInput[2]{};
                for (auto &input : pauseInput)
                {
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = VK_PAUSE;
                }
                pauseInput[1].ki.dwFlags = KEYEVENTF_KEYUP;
                if (SendInput(2, pauseInput, sizeof(INPUT)) != 2)
                    throw std::runtime_error("Windows rejected the Pause hotkey delivery test.");
                bool delivered = false;
                const auto deadline = GetTickCount64() + 2000;
                while (!delivered && GetTickCount64() < deadline)
                {
                    if (PeekMessageW(&hotkeyMessage, window, WM_HOTKEY, WM_HOTKEY, PM_REMOVE))
                    {
                        if (hotkeyMessage.wParam == static_cast<WPARAM>(app.instantHotkeyId))
                        {
                            DispatchMessageW(&hotkeyMessage);
                            delivered = true;
                        }
                    }
                    else
                        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT,
                                                    MWMO_INPUTAVAILABLE);
                }
                if (!delivered)
                    throw std::runtime_error(
                        "Windows did not deliver the registered Pause hotkey.");
                if (app.overlay || app.capturePending || app.image.empty() ||
                    app.image.width != GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
                    app.image.height != GetSystemMetrics(SM_CYVIRTUALSCREEN))
                    throw std::runtime_error(
                        "Saved Pause shortcut did not trigger instant all-monitor capture.");
                app.iniPath = originalIni;
                app.palette = PersistenceTestPalette;
                app.paletteDirty = true;
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
                // Earlier export tests exercise both states; explicitly establish this fixture.
                if (!app.exportOptions.professionalBorder)
                    command(ProfessionalBorder);
                command(ToggleTools);
                command(ToggleFormatting);
                command(LogoStyleFirst + 5);
                const auto closeColors = app.colors;
                const auto closeStyles = app.styles;
                if (app.exportOptions.professionalBlur)
                    command(ProfessionalBlur); // Persist a custom rounded-only combination on every
                                               // run.
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
                if (app.colors != closeColors || app.styles != closeStyles ||
                    app.tool != closedTool)
                    throw std::runtime_error(
                        "Closing lost tool colors/styles or changed the active tool.");
                if (app.fontSize != 40 || !app.textBold || !app.textBox ||
                    app.geometryTool != Tool::Rectangle || !app.exportOptions.professionalBorder ||
                    !app.exportOptions.samtecLogo || app.collapsedRows != 6 ||
                    app.exportOptions.professionalBlur || !app.exportOptions.professionalRounded ||
                    app.exportOptions.samtecStyle != 5)
                    throw std::runtime_error(
                        "Text formatting or geometry group preferences were lost.");
                // A later change must be written by the full-exit path, not the earlier close.
                app.image = Bitmap::create(10, 10);
                command(styleCommand(Tool::Check, 5));
                if (activeColor() != PersistenceTestColors[static_cast<size_t>(Tool::Check)])
                    throw std::runtime_error("Reopening a tool lost its previous custom color.");
                app.document.items.push_back(unsaved);
                app.dirty = true; // The final File > Exit must also proceed without a prompt.
                writeTestReport(
                    L"smoke-test-results.txt",
                    "PASS: unsaved new snips, closing, and File > Exit proceed without save "
                    "confirmation, "
                    "closing retains committed edits in session history, repeated capture "
                    "shortcuts ignored, "
                    "no editor/fade pixels in capture, restored editor with Pen selected, "
                    "instant hotkey freezes focus-dismissed hover popup pixels before selection, "
                    "instant all-monitor capture preserves hover menus and pointer with "
                    "visible/hidden and edited/unedited snips, "
                    "two independent shortcuts with swaps, duplicate/conflict rollback and "
                    "disable, explicit Pause key entry, Pause key, real Windows Pause hotkey "
                    "delivery, save/capture, undoable editor crop with translated annotations, "
                    "nested crops, reverse-direction mouse selection, DPI scaling, cancellation "
                    "and PNG fidelity, "
                    "visible and hidden editor, instant cancellation preserves the previous snip, "
                    "native window, Direct2D editor, live desktop capture, selection overlay "
                    "original pixels, mouse rectangle selection and cropping, mouse drawing, "
                    "yellow chisel highlight, "
                    "highlight toolbar/shortcut, transparency, cancellation, recoloring, width, "
                    "history, export, and persistence, all "
                    "stickers, red X click/drag/move/resize/delete/undo/redo, check/X default and "
                    "custom colors, "
                    "solid/dashed/dotted lines and endpoint editing, outlined/curved "
                    "arrows, straight and block gloss arrows, "
                    "move/resize/recolor, arrow endpoint rotation, delete, held +/- font/brush "
                    "repeat, bounds, pause/resume, cancellation, single-step undo, toolbar "
                    "press/release "
                    "and cancellation, mouse undo/redo, eyedropper toolbar/shortcut/cancel, "
                    "sampling image and annotation colors at multiple zoom levels, undoable picker "
                    "recolor, "
                    "pen cursor color and size, click-to-type text, click-away commits and returns "
                    "to Select, "
                    "matching native/export annotation typefaces, auto-sized borderless inline "
                    "editing, "
                    "native plain/boxed/multiline typing backgrounds, growth/deletion/line "
                    "wrapping, "
                    "live font/bold/color/box formatting, "
                    "Ctrl+Enter, double-click editing, Escape rollback, text "
                    "undo/redo/move/resize, "
                    "rectangle styles, native visual shape dropdowns, compact text layouts at "
                    "100/150/200% DPI, "
                    "annotated PNG export, Professional Border Settings toggle/checkmark, "
                    "actual PNG save with every independent blur/rounding combination, "
                    "professional submenu and persistence, Samtec Logo toggle/checkmark, six "
                    "visual styles and PNG Save, "
                    "styled preview/export equality for all settings and logo styles, alpha "
                    "compositing, cache invalidation, copy flash visibility, expiry, and unchanged "
                    "exports, "
                    "save location menu, folder preference, existing-file preservation, "
                    "unavailable-folder fallback, "
                    "PNG save in configured folder, plain wheel zoom, fitted minimum and 800% "
                    "maximum, hand panning, bounded edges, centered fitting axes, annotation hit "
                    "priority, "
                    "all collapsed row combinations at 100/150/200% DPI, full screen F11/Esc and "
                    "window restoration, "
                    "rendering dialog Apply/Cancel, live hardware/software switching and saved "
                    "preference, "
                    "unchanged snip/annotations/history/selection/view/export during renderer "
                    "changes, "
                    "settings dialog and persistence, per-tool styles/custom colors "
                    "saved on close, selected-shape recoloring remembers the correct tool.\n");
                SetTimer(window, SmokeTimer, 1000, nullptr);
            }
            MSG message{};
            while ([&] {
                const int received = GetMessageW(&message, nullptr, 0, 0);
                return messageAvailable(received, received == -1 ? GetLastError() : 0);
            }())
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
        if (selfTest || app.smoke || verifyPreferences || verifyPalette || penSizeTest ||
            verifyPenSize || app.resizeTest)
            failedTestReport(testReportDirectory /
                                 (recentTest          ? L"recent-test-results.txt"
                                  : arrowEditTest     ? L"arrow-edit-test-results.txt"
                                  : penSizeTest       ? L"pen-size-test-results.txt"
                                  : verifyPenSize     ? L"pen-size-preference-results.txt"
                                  : eraserTest        ? L"eraser-test-results.txt"
                                  : verifyPalette     ? L"palette-preference-results.txt"
                                  : paletteTest       ? L"palette-test-results.txt"
                                  : navigationTest    ? L"navigation-test-results.txt"
                                  : shortcutTest      ? L"shortcut-test-results.txt"
                                  : app.resizeTest    ? L"resize-test-results.txt"
                                  : verifyPreferences ? L"preference-test-results.txt"
                                  : selfTest          ? L"self-test-results.txt"
                                                      : L"smoke-test-results.txt"),
                             exception.what());
        else
            error(app.window, exception.what());
        if (app.window && IsWindow(app.window))
        {
            app.dirty = false;
            DestroyWindow(app.window);
        }
    }
    resetPreview();
    app.workspaceBrush.reset();
    app.target.reset();
    app.graphics.roundStroke.reset();
    app.graphics.dashStroke.reset();
    app.graphics.dotStroke.reset();
    app.graphics.labelFont.reset();
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
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    return callbackBoundary<int>([&] { return applicationMain(instance, show); },
                                 [](const char *failure) { showError(nullptr, failure); }, 1);
}
