#include "graphics.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

// Clipboard state belongs to a window station. This test uses a private, noninteractive
// station so it can test the actual Windows clipboard without replacing the user's data.
int wmain()
{
    HWINSTA originalStation = GetProcessWindowStation();
    HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    HWND owner = nullptr;
    bool com = false, clipboardOpen = false;
    int result = 0;
    try
    {
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        if (!station || !SetProcessWindowStation(station))
            throw std::runtime_error("Cannot create isolated clipboard window station.");
        desktop = CreateDesktopW(L"TigerSnipClipboardTest", nullptr, nullptr, 0,
                                 DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS,
                                 nullptr);
        if (!desktop || !SetThreadDesktop(desktop))
            throw std::runtime_error("Cannot attach isolated clipboard desktop.");
        owner = CreateWindowExW(0, L"STATIC", L"Clipboard test owner", WS_POPUP, 0, 0, 32, 32,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!owner)
            throw std::runtime_error("Cannot create clipboard owner.");
        snip::check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        snip::Graphics graphics;
        auto image = snip::Bitmap::create(48, 32);
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x)
            {
                size_t i = (static_cast<size_t>(y) * image.width + x) * 4;
                image.pixels[i] = static_cast<uint8_t>(x * 5);
                image.pixels[i + 1] = static_cast<uint8_t>(y * 7);
                image.pixels[i + 2] = static_cast<uint8_t>((x + y) * 3);
                image.pixels[i + 3] = 255;
            }
        auto verify = [&](const snip::Bitmap &image) {
            auto png = graphics.png(image);
            if (!snip::copyBitmap(owner, image, png) || !OpenClipboard(owner))
                throw std::runtime_error("Copy or clipboard read failed.");
            clipboardOpen = true;
            if (EnumClipboardFormats(0) != RegisterClipboardFormatW(L"PNG"))
                throw std::runtime_error("Clipboard did not offer transparent PNG first.");
            for (UINT format : {static_cast<UINT>(CF_DIB), static_cast<UINT>(CF_DIBV5)})
            {
                HANDLE memory = GetClipboardData(format);
                auto data = static_cast<const uint8_t *>(GlobalLock(memory));
                if (!data)
                    throw std::runtime_error("Clipboard DIB format missing.");
                const auto *header = reinterpret_cast<const BITMAPINFOHEADER *>(data);
                size_t size = format == CF_DIB ? sizeof(BITMAPINFOHEADER) : sizeof(BITMAPV5HEADER);
                bool good = header->biSize == size && header->biWidth == image.width &&
                            header->biHeight == -image.height && header->biBitCount == 32 &&
                            GlobalSize(memory) >= size + image.pixels.size() &&
                            std::memcmp(data + size, image.pixels.data(), image.pixels.size()) == 0;
                if (format == CF_DIBV5)
                {
                    auto v5 = reinterpret_cast<const BITMAPV5HEADER *>(data);
                    good = good && v5->bV5RedMask == 0x00ff0000 && v5->bV5AlphaMask == 0xff000000 &&
                           v5->bV5CSType == LCS_sRGB;
                }
                GlobalUnlock(memory);
                if (!good)
                    throw std::runtime_error("Clipboard DIB pixels, orientation, or metadata failed.");
            }
            HANDLE pngMemory = GetClipboardData(RegisterClipboardFormatW(L"PNG"));
            auto pngData = static_cast<const uint8_t *>(GlobalLock(pngMemory));
            if (!pngData)
                throw std::runtime_error("Clipboard PNG missing.");
            bool samePng = GlobalSize(pngMemory) >= png.size() &&
                           std::memcmp(pngData, png.data(), png.size()) == 0;
            GlobalUnlock(pngMemory);
            if (!samePng || graphics.decode(png).pixels != image.pixels)
                throw std::runtime_error("Clipboard PNG round trip failed.");
            CloseClipboard();
            clipboardOpen = false;
            };
        verify(graphics.exportImage(image, {}));
        const auto bordered = graphics.exportImage(image, {}, {true});
        if (bordered.width != image.width + 40 || bordered.height != image.height + 40 ||
            bordered.pixels[3] || bordered.pixels[(20 * bordered.width + 20) * 4 + 3] == 255)
            throw std::runtime_error("Clipboard export lost transparent padding or rounded corners.");
        verify(bordered);
        for (bool border : {false, true})
            for (uint8_t style = 0; style < 6; ++style)
                verify(graphics.exportImage(image, {}, {border, true, style}));
        for (bool blur : {false, true})
            for (bool rounded : {false, true})
            {
                snip::ExportOptions options{true};
                options.professionalBlur = blur;
                options.professionalRounded = rounded;
                verify(graphics.exportImage(image, {}, options));
                options.samtecLogo = true;
                verify(graphics.exportImage(image, {}, options));
            }
        std::cout << "PASS: isolated Windows clipboard; DIB and DIBV5 dimensions, top-down pixels, "
                     "alpha and color metadata; PNG-first payload and pixel-perfect round trip "
                     "with independent blur/rounding and Professional Border and Samtec Logo OFF and ON. "
                     "User clipboard untouched.\n";
    }
    catch (const std::exception &exception)
    {
        std::cout << "FAIL: " << exception.what() << " Windows error=" << GetLastError() << '\n';
        result = 1;
    }
    if (clipboardOpen)
        CloseClipboard();
    if (com)
        CoUninitialize();
    if (owner)
        DestroyWindow(owner);
    SetThreadDesktop(originalDesktop);
    if (desktop)
        CloseDesktop(desktop);
    SetProcessWindowStation(originalStation);
    if (station)
        CloseWindowStation(station);
    return result;
}
