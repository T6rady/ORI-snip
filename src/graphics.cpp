#include "graphics.h"
#include <objbase.h>
#include <cstring>
#include <stdexcept>

namespace snip
{
void check(HRESULT hr, const char *operation)
{
    if (FAILED(hr))
        throw std::runtime_error(operation);
}
D2D1_COLOR_F color(Color c, float alpha)
{
    return D2D1::ColorF((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f,
                        alpha);
}
void Graphics::initialize()
{
    if (factory)
        return;
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()),
          "Cannot initialize Direct2D.");
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown **>(textFactory.put())),
          "Cannot initialize text rendering.");
    auto makeFont = [&](Com<IDWriteTextFormat> &f, float size, DWRITE_FONT_WEIGHT weight) {
        check(textFactory->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", f.put()),
              "Cannot create UI font.");
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    };
    makeFont(font, 13, DWRITE_FONT_WEIGHT_MEDIUM);
    makeFont(smallFont, 12, DWRITE_FONT_WEIGHT_NORMAL);
    makeFont(titleFont, 28, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    makeFont(labelFont, 10, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    check(factory->CreateStrokeStyle(
              D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                          D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
              nullptr, 0, roundStroke.put()),
          "Cannot initialize brush strokes.");
    auto dashed = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_DASH);
    check(factory->CreateStrokeStyle(dashed, nullptr, 0, dashStroke.put()),
          "Cannot initialize dashed strokes.");
    dashed.dashStyle = D2D1_DASH_STYLE_DOT;
    check(factory->CreateStrokeStyle(dashed, nullptr, 0, dotStroke.put()),
          "Cannot initialize dotted strokes.");
}
void Graphics::drawAnnotations(ID2D1RenderTarget *rt, const std::vector<Annotation> &items)
{
    Com<ID2D1SolidColorBrush> brush;
    check(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.put()),
          "Cannot create drawing brush.");
    for (const auto &item : items)
    {
        brush->SetColor(color(item.color));
        float width = item.thickness;
        auto r = item.bounds();
        auto line = [&](Point a, Point b, float w, ID2D1StrokeStyle *stroke = nullptr) {
            rt->DrawLine({a.x, a.y}, {b.x, b.y}, brush.get(), w,
                         stroke ? stroke : roundStroke.get());
        };
        switch (item.kind)
        {
        case Tool::Pen:
            if (item.points.size() == 1)
                rt->FillEllipse(
                    D2D1::Ellipse({item.points[0].x, item.points[0].y}, width / 2, width / 2),
                    brush.get());
            else if (item.points.size() > 1)
            {
                Com<ID2D1PathGeometry> path;
                Com<ID2D1GeometrySink> sink;
                check(factory->CreatePathGeometry(path.put()), "Cannot create pen path.");
                check(path->Open(sink.put()), "Cannot draw pen path.");
                sink->BeginFigure({item.points[0].x, item.points[0].y}, D2D1_FIGURE_BEGIN_HOLLOW);
                for (size_t i = 1; i < item.points.size(); ++i)
                    sink->AddLine({item.points[i].x, item.points[i].y});
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                check(sink->Close(), "Cannot finish pen path.");
                rt->DrawGeometry(path.get(), brush.get(), width, roundStroke.get());
            }
            break;
        case Tool::Circle:
        {
            auto ellipse = D2D1::Ellipse({(r.left + r.right) / 2, (r.top + r.bottom) / 2},
                                         r.width() / 2, r.height() / 2);
            if (item.style == 1)
            {
                brush->SetColor(color(item.color, .18f));
                rt->FillEllipse(ellipse, brush.get());
                brush->SetColor(color(item.color));
                rt->DrawEllipse(ellipse, brush.get(), width * 1.2f, roundStroke.get());
            }
            else
                rt->DrawEllipse(ellipse, brush.get(), width,
                                item.style == 2 ? dashStroke.get() : roundStroke.get());
            break;
        }
        case Tool::Arrow: {
            Point v = item.b - item.a;
            float len = length(v);
            if (item.style != 0 && len > .01f)
            {
                auto outline = item.arrowContour();
                Com<ID2D1PathGeometry> path;
                Com<ID2D1GeometrySink> sink;
                check(factory->CreatePathGeometry(path.put()), "Cannot create arrow geometry.");
                check(path->Open(sink.put()), "Cannot draw arrow geometry.");
                sink->BeginFigure({outline[0].x, outline[0].y}, D2D1_FIGURE_BEGIN_FILLED);
                for (size_t i = 1; i < outline.size(); ++i)
                    sink->AddLine({outline[i].x, outline[i].y});
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                check(sink->Close(), "Cannot finish arrow geometry.");
                rt->FillGeometry(path.get(), brush.get());
                brush->SetColor(color(rgb(12, 12, 16)));
                rt->DrawGeometry(path.get(), brush.get(), std::min(len * .06f, std::max(1.2f, width * .45f)),
                                 roundStroke.get());
                if (item.style == 2 || item.style == 3)
                {
                    const Color c = item.color;
                    brush->SetColor(color(rgb((c & 255) / 2 + 127, ((c >> 8) & 255) / 2 + 127,
                                             ((c >> 16) & 255) / 2 + 127), .85f));
                    Point previous = item.arrowSpine(.22f);
                    float end = std::clamp(1 - std::min(len * .45f,
                        std::max({22.0f, width * 6, len * .18f})) / len, .55f, .84f) * .88f;
                    for (int i = 1; i <= 32; ++i)
                    {
                        Point next = item.arrowSpine(.22f + (end - .22f) * i / 32.0f);
                        line(previous, next, std::min(len * .012f, std::max(1.0f, width * .5f)));
                        previous = next;
                    }
                }
            }
            else
            {
                line(item.a, item.b, width);
                if (len > .01f)
                {
                    Point u = v * (1 / len), n{-u.y, u.x};
                    float head = std::min(len * .45f, std::max(12.0f, width * 3));
                    line(item.b, item.b - u * head + n * (head * .5f), width);
                    line(item.b, item.b - u * head - n * (head * .5f), width);
                }
            }
            break;
        }
        case Tool::Line:
            line(item.a, item.b, width,
                 item.style == 1 ? dashStroke.get() : item.style == 2 ? dotStroke.get() : nullptr);
            break;
        case Tool::Check: {
            float w = r.width(), h = r.height(), stroke = std::max(1.0f, std::min(w, h) * .06f);
            if (item.style == 0)
                rt->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), w * .12f,
                                      h * .12f),
                    brush.get(), stroke);
            else if (item.style == 1)
                rt->DrawEllipse(D2D1::Ellipse({(r.left + r.right) / 2, (r.top + r.bottom) / 2},
                                              w * .48f, h * .48f),
                                brush.get(), stroke);
            line({r.left + w * .23f, r.top + h * .52f}, {r.left + w * .43f, r.top + h * .72f},
                 stroke * (item.style == 2 ? 1.8f : 1.4f));
            line({r.left + w * .43f, r.top + h * .72f}, {r.left + w * .79f, r.top + h * .28f},
                 stroke * (item.style == 2 ? 1.8f : 1.4f));
            break;
        }
        default:
            break;
        }
    }
}
static Com<IWICImagingFactory> wicFactory()
{
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           __uuidof(IWICImagingFactory), reinterpret_cast<void **>(wic.put())),
          "Cannot initialize Windows image encoding.");
    return wic;
}
Bitmap Graphics::flatten(const Bitmap &image, const std::vector<Annotation> &items)
{
    if (image.empty())
        throw std::runtime_error("Take a snip first.");
    if (items.empty())
        return image;
    initialize();
    auto wic = wicFactory();
    Com<IWICBitmap> bitmap;
    check(wic->CreateBitmap(image.width, image.height, GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapCacheOnLoad, bitmap.put()),
          "Cannot create export image.");
    Com<ID2D1RenderTarget> rt;
    auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    check(factory->CreateWicBitmapRenderTarget(bitmap.get(), properties, rt.put()),
          "Cannot initialize image export renderer.");
    Com<ID2D1Bitmap> base;
    check(rt->CreateBitmap(D2D1::SizeU(image.width, image.height), image.pixels.data(),
                           image.width * 4,
                           D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                    D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                  96, 96),
                           base.put()),
          "Cannot load screenshot into export renderer.");
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(1, 1, 1));
    rt->DrawBitmap(
        base.get(),
        D2D1::RectF(0, 0, static_cast<float>(image.width), static_cast<float>(image.height)), 1,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    drawAnnotations(rt.get(), items);
    check(rt->EndDraw(), "Cannot render annotations.");
    Bitmap result = Bitmap::create(image.width, image.height);
    check(bitmap->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(result.pixels.size()),
                             result.pixels.data()),
          "Cannot read exported image.");
    return result;
}
std::vector<uint8_t> Graphics::png(const Bitmap &bitmap)
{
    auto wic = wicFactory();
    Com<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()), "Cannot allocate PNG stream.");
    Com<IWICBitmapEncoder> encoder;
    check(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()),
          "Cannot create PNG encoder.");
    check(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache),
          "Cannot initialize PNG encoder.");
    Com<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(frame.put(), nullptr), "Cannot create PNG frame.");
    check(frame->Initialize(nullptr), "Cannot initialize PNG frame.");
    check(frame->SetSize(bitmap.width, bitmap.height), "Cannot set PNG size.");
    check(frame->SetResolution(96, 96), "Cannot set PNG resolution.");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "Cannot set PNG pixel format.");
    // WIC's PNG encoder may select a different pixel format; convert through a bitmap source.
    Com<IWICBitmap> source;
    check(wic->CreateBitmapFromMemory(bitmap.width, bitmap.height, GUID_WICPixelFormat32bppBGRA,
                                      bitmap.width * 4, static_cast<UINT>(bitmap.pixels.size()),
                                      const_cast<BYTE *>(bitmap.pixels.data()), source.put()),
          "Cannot create PNG source.");
    Com<IWICFormatConverter> converter;
    check(wic->CreateFormatConverter(converter.put()), "Cannot create PNG converter.");
    check(converter->Initialize(source.get(), format, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom),
          "Cannot convert PNG pixels.");
    check(frame->WriteSource(converter.get(), nullptr), "Cannot encode PNG pixels.");
    check(frame->Commit(), "Cannot finish PNG frame.");
    check(encoder->Commit(), "Cannot finish PNG file.");
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME), "Cannot read PNG length.");
    if (stat.cbSize.QuadPart > UINT32_MAX)
        throw std::runtime_error("PNG file is too large.");
    std::vector<uint8_t> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "Cannot rewind PNG stream.");
    ULONG read = 0;
    check(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read),
          "Cannot read PNG stream.");
    if (read != bytes.size())
        throw std::runtime_error("PNG encoding was incomplete.");
    return bytes;
}
Bitmap Graphics::decode(const std::vector<uint8_t> &bytes)
{
    auto wic = wicFactory();
    Com<IWICStream> stream;
    check(wic->CreateStream(stream.put()), "Decode stream failed.");
    check(stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()),
                                       static_cast<DWORD>(bytes.size())),
          "Decode stream data failed.");
    Com<IWICBitmapDecoder> decoder;
    check(wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                       decoder.put()),
          "PNG decoding failed.");
    Com<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.put()), "Decode frame failed.");
    UINT w = 0, h = 0;
    check(frame->GetSize(&w, &h), "Decode size failed.");
    auto result = Bitmap::create(w, h);
    Com<IWICFormatConverter> converter;
    check(wic->CreateFormatConverter(converter.put()), "Decode converter failed.");
    check(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom),
          "Decode conversion failed.");
    check(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(result.pixels.size()),
                                result.pixels.data()),
          "Decode pixels failed.");
    return result;
}
Bitmap captureDesktop(int x, int y, int width, int height)
{
    auto result = Bitmap::create(width, height);
    HDC screen = GetDC(nullptr);
    if (!screen)
        throw std::runtime_error("Cannot access the desktop.");
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
        if (memory)
            DeleteDC(memory);
        if (bitmap)
            DeleteObject(bitmap);
        ReleaseDC(nullptr, screen);
        throw std::runtime_error("Cannot allocate the screen capture.");
    }
    auto previous = SelectObject(memory, bitmap);
    BOOL success = BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    if (success)
        std::memcpy(result.pixels.data(), pixels, result.pixels.size());
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!success)
        throw std::runtime_error("Windows could not capture the desktop.");
    for (size_t i = 3; i < result.pixels.size(); i += 4)
        result.pixels[i] = 255;
    return result;
}
void saveBytes(const std::wstring &path, const std::vector<uint8_t> &bytes)
{
    // Write beside the destination, then atomically replace it so failed saves preserve the old
    // file.
    std::wstring temporary =
        path + L".jack-snip-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot write this location. Choose another folder.");
    DWORD written = 0;
    bool success =
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success || !MoveFileExW(temporary.c_str(), path.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error(
            "Could not finish saving the PNG. The original file was preserved.");
    }
}
bool copyBitmap(HWND owner, const Bitmap &bitmap, const std::vector<uint8_t> &png)
{
    const size_t imageSize = bitmap.pixels.size();
    auto allocate = [&](const void *header, size_t headerSize, const uint8_t *pixels,
                        size_t count) -> HGLOBAL {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, headerSize + count);
        if (!memory)
            return nullptr;
        auto data = static_cast<uint8_t *>(GlobalLock(memory));
        if (!data)
        {
            GlobalFree(memory);
            return nullptr;
        }
        if (headerSize)
            std::memcpy(data, header, headerSize);
        std::memcpy(data + headerSize, pixels, count);
        GlobalUnlock(memory);
        return memory;
    };
    BITMAPV5HEADER v5{};
    v5.bV5Size = sizeof(v5);
    v5.bV5Width = bitmap.width;
    v5.bV5Height = -bitmap.height;
    v5.bV5Planes = 1;
    v5.bV5BitCount = 32;
    v5.bV5Compression = BI_BITFIELDS;
    v5.bV5SizeImage = static_cast<DWORD>(imageSize);
    v5.bV5RedMask = 0x00ff0000;
    v5.bV5GreenMask = 0x0000ff00;
    v5.bV5BlueMask = 0x000000ff;
    v5.bV5AlphaMask = 0xff000000;
    v5.bV5CSType = LCS_sRGB;
    v5.bV5Intent = LCS_GM_IMAGES;
    BITMAPINFOHEADER dib{};
    dib.biSize = sizeof(dib);
    dib.biWidth = bitmap.width;
    dib.biHeight = -bitmap.height;
    dib.biPlanes = 1;
    dib.biBitCount = 32;
    dib.biCompression = BI_RGB;
    dib.biSizeImage = static_cast<DWORD>(imageSize);
    HGLOBAL hV5 = allocate(&v5, sizeof(v5), bitmap.pixels.data(), imageSize),
            hDib = allocate(&dib, sizeof(dib), bitmap.pixels.data(), imageSize),
            hPng = allocate(nullptr, 0, png.data(), png.size());
    if (!hV5 || !hDib || !hPng)
    {
        if (hV5)
            GlobalFree(hV5);
        if (hDib)
            GlobalFree(hDib);
        if (hPng)
            GlobalFree(hPng);
        throw std::runtime_error("Not enough memory to copy the image.");
    }
    UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (!OpenClipboard(owner))
    {
        GlobalFree(hV5);
        GlobalFree(hDib);
        GlobalFree(hPng);
        return false;
    }
    if (!EmptyClipboard())
    {
        CloseClipboard();
        GlobalFree(hV5);
        GlobalFree(hDib);
        GlobalFree(hPng);
        return false;
    }
    bool success = false;
    if (SetClipboardData(CF_DIBV5, hV5))
    {
        hV5 = nullptr;
        success = true;
    }
    if (SetClipboardData(CF_DIB, hDib))
    {
        hDib = nullptr;
        success = true;
    }
    if (pngFormat && SetClipboardData(pngFormat, hPng))
    {
        hPng = nullptr;
        success = true;
    }
    CloseClipboard();
    if (hV5)
        GlobalFree(hV5);
    if (hDib)
        GlobalFree(hDib);
    if (hPng)
        GlobalFree(hPng);
    return success;
}
void Graphics::test()
{
    runModelTests();
    auto source = Bitmap::create(160, 120);
    std::fill(source.pixels.begin(), source.pixels.end(), 255);
    Annotation pen;
    pen.kind = Tool::Pen;
    pen.color = rgb(255, 0, 0);
    pen.thickness = 8;
    pen.points = {{10, 10}, {100, 10}};
    Annotation circle;
    circle.kind = Tool::Circle;
    circle.color = rgb(0, 0, 255);
    circle.thickness = 6;
    circle.a = {20, 30};
    circle.b = {60, 70};
    Annotation arrow;
    arrow.kind = Tool::Arrow;
    arrow.color = rgb(0, 255, 0);
    arrow.thickness = 6;
    arrow.a = {80, 30};
    arrow.b = {130, 80};
    Annotation mark;
    mark.kind = Tool::Check;
    mark.color = rgb(0, 128, 0);
    mark.a = {10, 80};
    mark.b = {40, 110};
    auto flattened = flatten(source, {pen, circle, arrow, mark});
    auto encoded = png(flattened);
    auto decoded = decode(encoded);
    if (decoded.width != 160 || decoded.height != 120 || decoded.pixels != flattened.pixels)
        throw std::runtime_error("PNG round-trip test failed.");
    auto pixel = [&](int x, int y, Color c) {
        size_t i = (static_cast<size_t>(y) * 160 + x) * 4;
        return flattened.pixels[i] == ((c >> 16) & 255) &&
               flattened.pixels[i + 1] == ((c >> 8) & 255) &&
               flattened.pixels[i + 2] == (c & 255) && flattened.pixels[i + 3] == 255;
    };
    if (!pixel(50, 10, rgb(255, 0, 0)))
        throw std::runtime_error("Pen export pixel test failed.");
    if (!pixel(20, 50, rgb(0, 0, 255)))
        throw std::runtime_error("Circle export pixel test failed.");
    if (!pixel(105, 55, rgb(0, 255, 0)))
        throw std::runtime_error("Arrow export pixel test failed.");
    size_t checkPixel = (95 * 160 + 10) * 4;
    if (flattened.pixels[checkPixel] > 100 || flattened.pixels[checkPixel + 2] > 100 ||
        flattened.pixels[checkPixel + 1] < 100 || flattened.pixels[checkPixel + 1] > 180)
        throw std::runtime_error("Check export pixel test failed.");
    if (!pixel(159, 119, rgb(255, 255, 255)))
        throw std::runtime_error("Image background export test failed.");
    pen.move({0, 20});
    auto moved = flatten(source, {pen});
    size_t old = (10 * 160 + 50) * 4, next = (30 * 160 + 50) * 4;
    if (moved.pixels[old] != 255 || moved.pixels[next] != 0 || moved.pixels[next + 2] != 255)
        throw std::runtime_error("Moved sticker export test failed.");
    auto preview = Bitmap::create(1280, 360);
    std::fill(preview.pixels.begin(), preview.pixels.end(), 255);
    std::vector<Annotation> samples;
    for (int style = 0; style < 4; ++style)
    {
        Annotation sample;
        sample.kind = Tool::Arrow;
        sample.style = static_cast<uint8_t>(style);
        sample.thickness = 6;
        sample.color = rgb(239, 45, 45);
        sample.a = {60.0f + style * 320, 100};
        sample.b = {270.0f + style * 320, 100};
        if (style == 2)
        {
            sample.a = {885, 40};
            sample.b = {725, 190};
        }
        if (!sample.hit(sample.arrowSpine(.5f), 0) || sample.hit({0, 359}, 0))
            throw std::runtime_error("Arrow artwork hit testing failed.");
        samples.push_back(sample);
        if (style < 3)
        {
            sample.kind = Tool::Line;
            sample.color = rgb(37, 99, 235);
            sample.a = {60.0f + style * 320, 285};
            sample.b = {270.0f + style * 320, 285};
            samples.push_back(sample);
        }
    }
    auto rendered = flatten(preview, samples);
    int runs[3]{};
    for (int style = 0; style < 3; ++style)
    {
        bool previous = false;
        for (int x = 50 + style * 320; x < 280 + style * 320; ++x)
        {
            size_t i = (static_cast<size_t>(285) * rendered.width + x) * 4;
            bool blue = rendered.pixels[i] > 180 && rendered.pixels[i + 1] < 150 &&
                        rendered.pixels[i + 2] < 100;
            if (blue && !previous)
                ++runs[style];
            previous = blue;
        }
        if (style)
        {
            int black = 0, red = 0;
            for (int y = 15; y < 225; ++y)
                for (int x = style * 320; x < (style + 1) * 320; ++x)
                {
                    size_t i = (static_cast<size_t>(y) * rendered.width + x) * 4;
                    if (rendered.pixels[i] < 40 && rendered.pixels[i + 1] < 40 &&
                        rendered.pixels[i + 2] < 40)
                        ++black;
                    if (rendered.pixels[i + 2] > 180 && rendered.pixels[i] < 100)
                        ++red;
                }
            if (black < 10 || red < 40)
                throw std::runtime_error("Outlined arrow export lost its border or fill.");
        }
    }
    if (runs[0] != 1 || runs[1] < 3 || runs[2] <= runs[1])
        throw std::runtime_error("Solid/dashed/dotted line export patterns failed.");
    const size_t shinePixel = (static_cast<size_t>(100) * rendered.width + 1125) * 4;
    if (rendered.pixels[shinePixel + 2] < 200 || rendered.pixels[shinePixel] < 80 ||
        rendered.pixels[shinePixel] > 200)
        throw std::runtime_error("Straight gloss arrow lost its highlight.");
    const Annotation &gloss = samples.back();
    if (length(gloss.arrowSpine(.5f) - Point{1125, 100}) > .001f ||
        gloss.hit({1125, 140}, 0) || !gloss.hit({1125, 100}, 0))
        throw std::runtime_error("Straight gloss arrow geometry or selection failed.");
    int glossBorder = 0, glossFill = 0;
    for (int y = 60; y < 145; ++y)
        for (int x = 1000; x < 1250; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * rendered.width + x) * 4;
            if (rendered.pixels[i] < 40 && rendered.pixels[i + 1] < 40 && rendered.pixels[i + 2] < 40)
                ++glossBorder;
            if (rendered.pixels[i + 2] > 180 && rendered.pixels[i] < 100)
                ++glossFill;
        }
    if (glossBorder < 10 || glossFill < 40 || decode(png(rendered)).pixels != rendered.pixels)
        throw std::runtime_error("Straight gloss arrow border/fill or PNG round trip failed.");
    saveBytes(L"annotation-style-preview.png", png(rendered));
}
} // namespace snip
