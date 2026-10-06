#include "capture.h"
#include "windows_support.h"
#include <cstring>
#include <stdexcept>

namespace snip
{
Bitmap captureDesktop(int x, int y, int width, int height, bool includeCursor)
{
    auto result = Bitmap::create(width, height);
    HDC screen = GetDC(nullptr);
    if (!screen)
        throwWindowsError("Cannot access the desktop.");
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!memory || !bitmap)
    {
        const DWORD failure = GetLastError();
        if (memory)
            DeleteDC(memory);
        if (bitmap)
            DeleteObject(bitmap);
        ReleaseDC(nullptr, screen);
        throwWindowsError("Cannot allocate the screen capture.", failure);
    }
    auto previous = SelectObject(memory, bitmap);
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    HICON cursorCopy = nullptr;
    ICONINFO cursorIcon{};
    if (includeCursor && GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING))
    {
        cursorCopy = CopyIcon(cursor.hCursor);
        if (cursorCopy)
            GetIconInfo(cursorCopy, &cursorIcon);
    }
    BOOL success = BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT);
    const DWORD failure = success ? ERROR_SUCCESS : GetLastError();
    if (success && cursorCopy && cursorIcon.hbmMask)
        DrawIconEx(memory, cursor.ptScreenPos.x - x - cursorIcon.xHotspot,
                   cursor.ptScreenPos.y - y - cursorIcon.yHotspot, cursorCopy, 0, 0, 0, nullptr,
                   DI_NORMAL);
    if (cursorIcon.hbmColor)
        DeleteObject(cursorIcon.hbmColor);
    if (cursorIcon.hbmMask)
        DeleteObject(cursorIcon.hbmMask);
    if (cursorCopy)
        DestroyIcon(cursorCopy);
    GdiFlush();
    if (success)
        std::memcpy(result.pixels.data(), pixels, result.pixels.size());
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!success)
        throwWindowsError("Windows could not capture the desktop.", failure);
    for (size_t i = 3; i < result.pixels.size(); i += 4)
        result.pixels[i] = 255;
    return result;
}
} // namespace snip
