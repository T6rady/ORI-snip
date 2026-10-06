#include "color_picker.h"
#include "windows_support.h"
#include "test_hooks.h"
#include <commctrl.h>
#include <windowsx.h>
#include <cwchar>
#include <stdexcept>

namespace snip
{
// The editor owns the saved palette. This dialog only chooses a single color.
std::wstring colorHex(Color value)
{
    wchar_t text[8]{};
    swprintf_s(text, L"#%02X%02X%02X", GetRValue(value), GetGValue(value), GetBValue(value));
    return text;
}
std::optional<Color> parseColorHex(std::wstring text)
{
    if (!text.empty() && text.front() == L'#')
        text.erase(0, 1);
    if (text.size() != 6)
        return {};
    unsigned value = 0;
    for (wchar_t c : text)
    {
        unsigned digit;
        if (c >= L'0' && c <= L'9')
            digit = c - L'0';
        else if (c >= L'a' && c <= L'f')
            digit = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F')
            digit = c - L'A' + 10;
        else
            return {};
        value = value * 16 + digit;
    }
    return rgb(value >> 16, (value >> 8) & 255, value & 255);
}
Color spectrumColor(float hue, float saturation, float value)
{
    const float chroma = value * saturation;
    const float h = hue / 60;
    const float x = chroma * (1 - std::abs(std::fmod(h, 2.0f) - 1));
    float r = 0, g = 0, b = 0;
    if (h < 1)
    {
        r = chroma;
        g = x;
    }
    else if (h < 2)
    {
        r = x;
        g = chroma;
    }
    else if (h < 3)
    {
        g = chroma;
        b = x;
    }
    else if (h < 4)
    {
        g = x;
        b = chroma;
    }
    else if (h < 5)
    {
        r = x;
        b = chroma;
    }
    else
    {
        r = chroma;
        b = x;
    }
    const float m = value - chroma;
    return rgb(static_cast<unsigned>(std::round((r + m) * 255)),
               static_cast<unsigned>(std::round((g + m) * 255)),
               static_cast<unsigned>(std::round((b + m) * 255)));
}
struct ColorPicker
{
    Color value;
    bool editing = false, syncing = false;
    float hue = 0, saturation = 0, brightness = 0;
    std::vector<uint32_t> spectrum;
    int spectrumWidth = 0, spectrumHeight = 0;
    float spectrumHue = -1;
    // Optional driver for the app's isolated smoke checks.
    void (*test)(HWND) = nullptr;

    void toSpectrum()
    {
        const float r = GetRValue(value) / 255.0f, g = GetGValue(value) / 255.0f,
                    b = GetBValue(value) / 255.0f;
        const float maximum = std::max({r, g, b}), minimum = std::min({r, g, b});
        const float delta = maximum - minimum;
        brightness = maximum;
        saturation = maximum > 0 ? delta / maximum : 0;
        if (delta > 0)
        {
            hue = 60 * (maximum == r   ? std::fmod((g - b) / delta, 6.0f)
                        : maximum == g ? (b - r) / delta + 2
                                       : (r - g) / delta + 4);
            if (hue < 0)
                hue += 360;
        }
    }
    void sync(HWND window, int source = 0)
    {
        syncing = true;
        if (source != 11)
            SetDlgItemTextW(window, 11, colorHex(value).c_str());
        if (source < 12 || source > 14)
            for (int i = 0; i < 3; ++i)
                SetDlgItemInt(window, 12 + i, (value >> (i * 8)) & 255, FALSE);
        SendDlgItemMessageW(window, 15, TBM_SETPOS, TRUE, static_cast<int>(std::round(hue)) % 360);
        EnableWindow(GetDlgItem(window, IDOK), TRUE);
        SetDlgItemTextW(window, 16, L"Colors are saved in your toolbar palette.");
        InvalidateRect(GetDlgItem(window, 10), nullptr, FALSE);
        InvalidateRect(GetDlgItem(window, 17), nullptr, FALSE);
        syncing = false;
    }
};
LRESULT CALLBACK spectrumProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR,
                                   DWORD_PTR context)
{
    return callbackBoundary<LRESULT>(
        [&]() -> LRESULT {
#ifdef TIGER_SNIP_TESTING
            if (testing::callbackCheckpoint)
                testing::callbackCheckpoint("spectrumProcedure", message);
#endif
            auto &picker = *reinterpret_cast<ColorPicker *>(context);
            if (message == WM_LBUTTONDOWN || (message == WM_MOUSEMOVE && GetCapture() == hwnd))
            {
                if (message == WM_LBUTTONDOWN)
                {
                    SetFocus(hwnd);
                    SetCapture(hwnd);
                }
                RECT r{};
                GetClientRect(hwnd, &r);
                picker.saturation = std::clamp(
                    GET_X_LPARAM(lp) / static_cast<float>(std::max(1L, r.right - 1)), 0.0f, 1.0f);
                picker.brightness =
                    1 -
                    std::clamp(GET_Y_LPARAM(lp) / static_cast<float>(std::max(1L, r.bottom - 1)),
                               0.0f, 1.0f);
                picker.value = spectrumColor(picker.hue, picker.saturation, picker.brightness);
                picker.sync(GetParent(hwnd));
                return 0;
            }
            if (message == WM_LBUTTONUP && GetCapture() == hwnd)
            {
                ReleaseCapture();
                return 0;
            }
            if (message == WM_NCDESTROY)
                RemoveWindowSubclass(hwnd, spectrumProcedure, 1);
            return DefSubclassProc(hwnd, message, wp, lp);
        },
        [&](const char *failure) {
            showError(GetParent(hwnd), failure);
            ReleaseCapture();
            EndDialog(GetParent(hwnd), IDCANCEL);
        },
        0);
}
INT_PTR CALLBACK colorPickerProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    return callbackBoundary<INT_PTR>(
        [&]() -> INT_PTR {
#ifdef TIGER_SNIP_TESTING
            if (testing::callbackCheckpoint)
                testing::callbackCheckpoint("colorPickerProcedure", message);
#endif
            auto *picker = reinterpret_cast<ColorPicker *>(GetWindowLongPtrW(window, DWLP_USER));
            if (message == WM_INITDIALOG)
            {
                picker = reinterpret_cast<ColorPicker *>(lp);
                SetWindowLongPtrW(window, DWLP_USER, lp);
                SetWindowTextW(window,
                               picker->editing ? L"Edit palette color" : L"Add palette color");
                SetDlgItemTextW(window, IDOK, picker->editing ? L"Save color" : L"Add color");
                SendDlgItemMessageW(window, 11, EM_SETLIMITTEXT, 7, 0);
                for (int id = 12; id <= 14; ++id)
                    SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, 3, 0);
                SendDlgItemMessageW(window, 15, TBM_SETRANGE, TRUE, MAKELPARAM(0, 359));
                SendDlgItemMessageW(window, 15, TBM_SETLINESIZE, 0, 1);
                SendDlgItemMessageW(window, 15, TBM_SETPAGESIZE, 0, 15);
                if (!SetWindowSubclass(GetDlgItem(window, 10), spectrumProcedure, 1,
                                       reinterpret_cast<DWORD_PTR>(picker)))
                    throwWindowsError("Cannot initialize the color spectrum.");
                picker->toSpectrum();
                picker->sync(window);
                RECT owner{}, bounds{};
                GetWindowRect(GetParent(window), &owner);
                GetWindowRect(window, &bounds);
                SetWindowPos(
                    window, nullptr,
                    owner.left + ((owner.right - owner.left) - (bounds.right - bounds.left)) / 2,
                    owner.top + ((owner.bottom - owner.top) - (bounds.bottom - bounds.top)) / 2, 0,
                    0, SWP_NOSIZE | SWP_NOZORDER);
                if (picker->test)
                    SetTimer(window, 1, 100, nullptr);
                return TRUE;
            }
            if (!picker)
                return FALSE;
            switch (message)
            {
            case WM_TIMER:
                if (picker->test)
                {
                    KillTimer(window, 1);
                    picker->test(window);
                }
                return TRUE;
            case WM_COMMAND:
                if (LOWORD(wp) == IDCANCEL)
                {
                    EndDialog(window, IDCANCEL);
                    return TRUE;
                }
                if (LOWORD(wp) == IDOK)
                {
                    if (IsWindowEnabled(GetDlgItem(window, IDOK)))
                        EndDialog(window, IDOK);
                    return TRUE;
                }
                if (HIWORD(wp) == EN_CHANGE && !picker->syncing)
                {
                    const int source = LOWORD(wp);
                    std::optional<Color> value;
                    if (source == 11)
                    {
                        wchar_t text[16]{};
                        GetDlgItemTextW(window, 11, text, 16);
                        value = parseColorHex(text);
                    }
                    else if (source >= 12 && source <= 14)
                    {
                        unsigned channels[3]{};
                        bool valid = true;
                        for (int i = 0; i < 3; ++i)
                        {
                            wchar_t text[16]{};
                            const int length = GetDlgItemTextW(window, 12 + i, text, 16);
                            valid &= length > 0 && length <= 3;
                            for (int j = 0; j < length; ++j)
                            {
                                valid &= text[j] >= L'0' && text[j] <= L'9';
                                if (text[j] >= L'0' && text[j] <= L'9')
                                    channels[i] = channels[i] * 10 + (text[j] - L'0');
                            }
                            valid &= channels[i] <= 255;
                        }
                        if (valid)
                            value = rgb(channels[0], channels[1], channels[2]);
                    }
                    else
                        return FALSE;
                    if (value)
                    {
                        picker->value = *value;
                        picker->toSpectrum();
                        picker->sync(window, source);
                    }
                    else
                    {
                        EnableWindow(GetDlgItem(window, IDOK), FALSE);
                        SetDlgItemTextW(window, 16,
                                        source == 11 ? L"Enter six hex digits, such as #EF4444."
                                                     : L"Red, green and blue must be 0 to 255.");
                    }
                    return TRUE;
                }
                break;
            case WM_HSCROLL:
                if (reinterpret_cast<HWND>(lp) == GetDlgItem(window, 15))
                {
                    picker->hue =
                        static_cast<float>(SendDlgItemMessageW(window, 15, TBM_GETPOS, 0, 0));
                    picker->value =
                        spectrumColor(picker->hue, picker->saturation, picker->brightness);
                    picker->sync(window);
                    return TRUE;
                }
                break;
            case WM_DRAWITEM: {
                const auto &item = *reinterpret_cast<DRAWITEMSTRUCT *>(lp);
                if (item.CtlID == 18)
                {
                    const int w = item.rcItem.right - item.rcItem.left;
                    for (int x = 0; x < w; ++x)
                    {
                        RECT strip{item.rcItem.left + x, item.rcItem.top, item.rcItem.left + x + 1,
                                   item.rcItem.bottom};
                        SetDCBrushColor(item.hDC,
                                        spectrumColor(x * 359.0f / std::max(1, w - 1), 1, 1));
                        FillRect(item.hDC, &strip, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                    }
                    return TRUE;
                }
                if (item.CtlID == 17)
                {
                    SetDCBrushColor(item.hDC, picker->value);
                    FillRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                    FrameRect(item.hDC, &item.rcItem, GetSysColorBrush(COLOR_3DSHADOW));
                    return TRUE;
                }
                if (item.CtlID != 10)
                    return FALSE;
                const int w = item.rcItem.right - item.rcItem.left,
                          h = item.rcItem.bottom - item.rcItem.top;
                if (picker->spectrumWidth != w || picker->spectrumHeight != h ||
                    picker->spectrumHue != picker->hue)
                {
                    picker->spectrum.resize(static_cast<size_t>(w) * h);
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x)
                        {
                            const Color c = spectrumColor(
                                picker->hue, x / static_cast<float>(std::max(1, w - 1)),
                                1 - y / static_cast<float>(std::max(1, h - 1)));
                            picker->spectrum[static_cast<size_t>(y) * w + x] =
                                (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
                        }
                    picker->spectrumWidth = w;
                    picker->spectrumHeight = h;
                    picker->spectrumHue = picker->hue;
                }
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = w;
                info.bmiHeader.biHeight = -h;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                SetDIBitsToDevice(item.hDC, item.rcItem.left, item.rcItem.top, w, h, 0, 0, 0, h,
                                  picker->spectrum.data(), &info, DIB_RGB_COLORS);
                const int x = item.rcItem.left + static_cast<int>(picker->saturation * (w - 1));
                const int y =
                    item.rcItem.top + static_cast<int>((1 - picker->brightness) * (h - 1));
                const auto oldBrush = SelectObject(item.hDC, GetStockObject(HOLLOW_BRUSH));
                const auto oldPen = SelectObject(item.hDC, GetStockObject(BLACK_PEN));
                Ellipse(item.hDC, x - 5, y - 5, x + 6, y + 6);
                SelectObject(item.hDC, GetStockObject(WHITE_PEN));
                Ellipse(item.hDC, x - 4, y - 4, x + 5, y + 5);
                SelectObject(item.hDC, oldPen);
                SelectObject(item.hDC, oldBrush);
                return TRUE;
            }
            }
            return FALSE;
        },
        [&](const char *failure) {
            showError(window, failure);
            EndDialog(window, IDCANCEL);
        },
        FALSE);
}
std::optional<Color> pickPaletteColor(HINSTANCE instance, HWND owner, Color value, bool editing,
                                      void (*test)(HWND))
{
    ColorPicker picker{};
    picker.value = value;
    picker.editing = editing;
    picker.test = test;
    const auto result = DialogBoxParamW(instance, MAKEINTRESOURCEW(301), owner,
                                        colorPickerProcedure, reinterpret_cast<LPARAM>(&picker));
    if (result == -1)
        throwWindowsError("Cannot open the palette color picker.");
    return result == IDOK ? std::optional<Color>(picker.value) : std::nullopt;
}
} // namespace snip
