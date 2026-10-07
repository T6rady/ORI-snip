// Repeatable typing benchmark and pixel checks on a private desktop/clipboard.
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
        require(station && SetProcessWindowStation(station), "Cannot isolate clipboard.");
        desktop = CreateDesktopW(L"TigerSnipTextTest", nullptr, nullptr, 0,
                                 DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS |
                                     DESKTOP_WRITEOBJECTS,
                                 nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot isolate desktop.");
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        app.instance = GetModuleHandleW(nullptr);
        app.smoke = true;
        app.softwareRendering = true;
        app.iniPath = (std::filesystem::current_path() / L"text-settings.ini").wstring();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        registerClasses();
        require(CreateWindowExW(0, MainClass, L"Text test", WS_OVERLAPPEDWINDOW, 0, 0, 1050, 740,
                                nullptr, createMenu(), app.instance, nullptr),
                "Cannot create editor.");
        app.image = Bitmap::create(3840, 2160);
        for (int y = 0; y < app.image.height; ++y)
            for (int x = 0; x < app.image.width; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * app.image.width + x) * 4;
                app.image.pixels[i] = static_cast<uint8_t>(x);
                app.image.pixels[i + 1] = static_cast<uint8_t>(y);
                app.image.pixels[i + 2] = 180;
                app.image.pixels[i + 3] = 255;
            }
        app.exportOptions = {};
        updateView();
        auto verify = [&](const std::wstring &name) {
            std::cout << "Checking " << std::string(name.begin(), name.end()) << "\n";
            const auto expected = app.graphics.exportImage(
                app.image, app.document.items, app.exportOptions, app.document.selected);
            require(previewImage().pixels == expected.pixels,
                    "Cached editing preview differs from a complete export.");
            const auto scene = renderEditorPreview();
            RECT field{};
            GetClientRect(app.textEdit, &field);
            POINT origin{};
            MapWindowPoints(app.textEdit, app.window, &origin, 1);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = field.right;
            info.bmiHeader.biHeight = -field.bottom;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void *bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            HDC dc = CreateCompatibleDC(nullptr);
            require(bitmap && bits && dc, "Cannot inspect inline background.");
            const auto previous = SelectObject(dc, bitmap);
            FillRect(dc, &field, app.textEditBackground);
            GdiFlush();
            auto pixels = static_cast<const uint8_t *>(bits);
            bool matches = true;
            size_t mismatches = 0;
            for (int y = 0; y < field.bottom; ++y)
                for (int x = 0; x < field.right; ++x)
                {
                    const int sx = std::clamp(origin.x + x, 0L, LONG(scene.width - 1));
                    const int sy = std::clamp(origin.y + y, 0L, LONG(scene.height - 1));
                    const size_t src = (static_cast<size_t>(sy) * scene.width + sx) * 4;
                    const size_t dst = (static_cast<size_t>(y) * field.right + x) * 4;
                    for (int c = 0; c < 3; ++c)
                        if (pixels[dst + c] != scene.pixels[src + c])
                        {
                            matches = false;
                            if (++mismatches == 1)
                                std::cout << "First background difference at " << x << "," << y
                                          << " channel " << c << ": " << int(pixels[dst + c])
                                          << " vs " << int(scene.pixels[src + c]) << "\n";
                        }
                }
            SendMessageW(app.textEdit, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc),
                         PRF_CLIENT | PRF_ERASEBKGND);
            GdiFlush();
            auto native = Bitmap::create(field.right, field.bottom);
            std::copy_n(pixels, native.pixels.size(), native.pixels.begin());
            for (size_t i = 3; i < native.pixels.size(); i += 4)
                native.pixels[i] = 255;
            SelectObject(dc, previous);
            DeleteDC(dc);
            DeleteObject(bitmap);
            saveBytes(name + L"-native.png", app.graphics.png(native));
            if (!matches)
                std::cout << "Background differing channels: " << mismatches << "\n";
            require(matches, "Inline background differs from full editor rendering.");
            saveBytes(name + L"-scene.png", app.graphics.png(scene));
        };
        for (int mode = 0; mode < 3; ++mode)
        {
            app.document.clear();
            resetPreview();
            app.exportOptions = mode == 2 ? ExportOptions{true, true, 5} : ExportOptions{};
            app.textBox = mode != 0;
            beginTextEditing({100, 100});
            std::vector<double> timings;
            for (int i = 0; i < 30; ++i)
            {
                const auto start = std::chrono::steady_clock::now();
                SendMessageW(app.textEdit, WM_CHAR, L'a', 0);
                timings.push_back(std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - start)
                                      .count());
            }
            std::sort(timings.begin(), timings.end());
            std::cout << (mode == 0   ? "Plain"
                          : mode == 1 ? "Boxed"
                                      : "Boxed with border/logo")
                      << " typing: median=" << timings[15] << "ms p95=" << timings[28]
                      << "ms max=" << timings.back() << "ms (30 chars)\n";
            verify(L"typing-" + std::to_wstring(mode));
            for (int i = 0; i < 20; ++i)
                SendMessageW(app.textEdit, WM_CHAR, VK_BACK, 0);
            verify(L"shrunk-" + std::to_wstring(mode));
            SendMessageW(app.textEdit, EM_SETSEL, 1, 7);
            verify(L"selected-" + std::to_wstring(mode));
            SendMessageW(app.textEdit, EM_REPLACESEL, TRUE,
                         reinterpret_cast<LPARAM>(
                             L"multiple words that wrap across several lines\r\nsecond line"));
            verify(L"multiline-" + std::to_wstring(mode));
            changeTextFormatting(32, true, mode != 0);
            changeColor(rgb(35, 70, 130));
            verify(L"formatted-" + std::to_wstring(mode));
            const auto expectedExport =
                app.graphics.exportImage(app.image, app.document.items, app.exportOptions);
            finishTextEditing();
            const auto exported =
                app.graphics.exportImage(app.image, app.document.items, app.exportOptions);
            require(exported.pixels == expectedExport.pixels,
                    "Commit altered final export pixels.");
            const auto png = app.graphics.png(exported);
            require(app.graphics.decode(png).pixels == exported.pixels, "PNG pixels changed.");
            saveBytes(L"export-" + std::to_wstring(mode) + L".png", png);
            require(copyBitmap(app.window, exported, png), "Clipboard export failed.");
            require(OpenClipboard(app.window), "Cannot inspect clipboard.");
            const auto memory = GetClipboardData(CF_DIB);
            const auto data = static_cast<const uint8_t *>(GlobalLock(memory));
            const auto header = reinterpret_cast<const BITMAPINFOHEADER *>(data);
            const bool dibMatches =
                data && header->biWidth == exported.width && header->biHeight == -exported.height &&
                std::equal(exported.pixels.begin(), exported.pixels.end(), data + sizeof(*header));
            if (data)
                GlobalUnlock(memory);
            const auto pngMemory = GetClipboardData(RegisterClipboardFormatW(L"PNG"));
            const auto pngData = static_cast<const uint8_t *>(GlobalLock(pngMemory));
            const bool pngMatches = pngData && GlobalSize(pngMemory) >= png.size() &&
                                    std::equal(png.begin(), png.end(), pngData);
            if (pngData)
                GlobalUnlock(pngMemory);
            CloseClipboard();
            require(dibMatches && pngMatches, "Clipboard pixels differ from final PNG.");
        }
        app.image = app.image.crop(0, 0, 800, 500);
        app.document.clear();
        app.exportOptions = {true, true, 5};
        Annotation rectangle;
        rectangle.kind = Tool::Rectangle;
        rectangle.a = {110.25f, 85.5f};
        rectangle.b = {310.25f, 210.5f};
        rectangle.style = 2;
        app.document.items.push_back(rectangle);
        Annotation text;
        text.kind = Tool::Text;
        text.a = {120.25f, 90.5f};
        text.text = L"Edit this text";
        text.boxed = true;
        app.graphics.measureText(text);
        app.document.items.push_back(text);
        rectangle.kind = Tool::Circle;
        rectangle.a = {150.75f, 100.25f};
        rectangle.b = {280.75f, 180.25f};
        app.document.items.push_back(rectangle); // Above the edited box in paint order.
        resetPreview();
        app.fit = true;
        updateView();
        beginTextEditing({}, 1);
        SendMessageW(app.textEdit, WM_CHAR, L'!', 0);
        verify(L"overlap");
        for (float dpi : {1.0f, 1.5f, 2.0f})
        {
            app.dpi = dpi;
            app.fit = true;
            updateView();
            syncTextEditor();
            SendMessageW(app.textEdit, WM_CHAR, L'X', 0);
            verify(L"dpi-" + std::to_wstring(int(dpi * 100)));
        }
        app.dpi = 1;
        app.fit = false;
        app.view.scale = 1.4f;
        app.view.origin = {-170.5f, toolbarHeight() + 20.25f};
        updateView();
        syncTextEditor();
        SendMessageW(app.textEdit, WM_CHAR, VK_BACK, 0);
        verify(L"zoom-pan");
        SendMessageW(app.textEdit, EM_SETSEL, 0, -1);
        SendMessageW(app.textEdit, WM_CHAR, VK_BACK, 0);
        verify(L"empty");
        finishTextEditing(true);
        require(app.document.items[1] == text, "Cancel did not restore original text.");
        applyCrop({20, 20, 780, 480});
        app.fit = true;
        updateView();
        beginTextEditing({}, 1);
        SendMessageW(app.textEdit, WM_CHAR, L'?', 0);
        verify(L"cropped");
        finishTextEditing();
        command(Undo);
        require(app.document.items[1].text == text.text, "Text edit undo failed.");
        command(Redo);
        require(app.document.items[1].text == text.text + L"?", "Text edit redo failed.");
        for (int location = 0; location < 2; ++location)
        {
            app.document.clear();
            resetPreview();
            app.textBox = true;
            app.fontSize = 24;
            app.textBold = false;
            app.fit = true;
            updateView();
            beginTextEditing(
                location == 0 ? Point{.25f, .5f}
                              : Point{float(app.image.width - 180), float(app.image.height - 90)});
            for (wchar_t character : std::wstring(L"Test"))
                SendMessageW(app.textEdit, WM_CHAR, character, 0);
            verify(location == 0 ? L"styled-edge" : L"logo-overlap");
            SendMessageW(app.textEdit, WM_CHAR, VK_BACK, 0);
            verify(location == 0 ? L"styled-edge-shrunk" : L"logo-overlap-shrunk");
            finishTextEditing();
        }
        require(!app.textEditBacking && !app.textEditTarget && !app.textEditDisplay &&
                    !app.textEditWorkspace,
                "Editing-session resources were not released.");
        // Text edges reflow/resize its frame independently; corners retain font scaling.
        auto lineCount = [&](const Annotation &item) {
            auto layout = app.graphics.textLayout(item);
            DWRITE_TEXT_METRICS metrics{};
            check(layout->GetMetrics(&metrics), "Cannot inspect text wrapping.");
            return metrics.lineCount;
        };
        auto mouse = [&](Point p) {
            const auto screen = app.view.toScreen(p) * app.dpi;
            return MAKELPARAM(static_cast<int>(std::lround(screen.x)),
                             static_cast<int>(std::lround(screen.y)));
        };
        auto dragTextEdge = [&](int handle, Point delta, bool cancel = false) {
            const auto start = handles(app.document.items[0])[handle];
            mouseDown(mouse(start));
            require(app.drag == Drag::Resize && app.handle == handle &&
                        GetCapture() == app.window && app.document.editing(),
                    "Text edge did not start a resize transaction.");
            mouseMove(mouse(start + delta * .5f));
            mouseMove(mouse(start + delta));
            if (cancel)
                processKey(VK_ESCAPE);
            else
                mouseUp(mouse(start + delta));
            require(app.drag == Drag::None && GetCapture() != app.window && !app.document.editing(),
                    "Text resizing leaked capture or a history transaction.");
        };
        for (int layout : {InterfaceClassic, InterfaceOrange})
            for (float dpi : {1.0f, 1.5f, 2.0f})
                for (bool boxed : {false, true})
                {
                    command(layout);
                    app.dpi = dpi;
                    SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(1050 * dpi),
                                 static_cast<int>(740 * dpi), SWP_NOZORDER | SWP_NOACTIVATE);
                    app.fit = true;
                    app.tool = Tool::Select;
                    app.document.clear();
                    Annotation wrapped;
                    wrapped.kind = Tool::Text;
                    wrapped.a = {60, 60};
                    wrapped.text = L"this is how test works";
                    wrapped.fontSize = 32;
                    wrapped.textWidth = 140;
                    wrapped.bold = true;
                    wrapped.boxed = boxed;
                    app.graphics.measureText(wrapped);
                    require(lineCount(wrapped) > 1, "Text resize fixture did not wrap.");
                    app.document.items = {wrapped};
                    app.document.selected = 0;
                    resetPreview();
                    updateView();
                    buildButtons();
                    const auto pixels = app.image.pixels;
                    const auto toolColors = app.colors;
                    const float fontPreference = app.fontSize;
                    const auto initial = app.graphics.flatten(app.image, {wrapped});
                    const auto centerRight = app.view.toScreen(handles(wrapped)[5]);
                    require(editorCursor(centerRight) == LoadCursorW(nullptr, IDC_SIZEWE),
                            "Text width handle did not show a horizontal resize cursor.");
                    dragTextEdge(5, {440, 80});
                    const auto wide = app.document.items[0];
                    require(wide.textFrame && wide.fontSize == wrapped.fontSize &&
                                wide.text == wrapped.text && wide.color == wrapped.color &&
                                wide.bold == wrapped.bold && wide.boxed == boxed &&
                                wide.a == wrapped.a && lineCount(wide) == 1 &&
                                wide.bounds().height() < wrapped.bounds().height() &&
                                std::abs(wide.bounds().width() - wrapped.bounds().width() - 440) <
                                    3,
                            "Widening a text box scaled the font, moved its anchor or failed to "
                            "rewrap.");
                    const auto wideExport = app.graphics.flatten(app.image, {wide});
                    require(wideExport.pixels != initial.pixels &&
                                previewImage().pixels == renderedExport().pixels &&
                                app.graphics.decode(app.graphics.png(wideExport)).pixels ==
                                    wideExport.pixels,
                            "Text width changes were missing from cached preview or PNG export.");
                    if (dpi == 1 && boxed)
                    {
                        saveBytes(app.classicUI ? L"text-top-reflow.png" : L"text-side-reflow.png",
                                  app.graphics.png(renderEditorPreview()));
                        saveBytes(app.classicUI ? L"text-top-reflow-export.png"
                                                : L"text-side-reflow-export.png",
                                  app.graphics.png(wideExport));
                    }
                    require(app.document.undo() && app.document.items[0] == wrapped &&
                                !app.document.canUndo(),
                            "A text width drag was not one undo step.");
                    require(app.document.redo() && app.document.items[0] == wide,
                            "Text width redo did not restore the frame and wrapping.");
                    app.document.selected = 0;
                    dragTextEdge(7, {-30, -80});
                    auto left = app.document.items[0];
                    require(std::abs(left.b.x - wide.b.x) < .01f && left.a.y == wide.a.y &&
                                left.fontSize == wide.fontSize && lineCount(left) == 1,
                            "Left text handle did not keep the right edge and font anchored.");
                    require(app.document.undo(), "Cannot undo left text resize.");
                    app.document.selected = 0;
                    dragTextEdge(6, {80, 100});
                    auto taller = app.document.items[0];
                    require(taller.a == wide.a && taller.b.x == wide.b.x &&
                                taller.fontSize == wide.fontSize && lineCount(taller) == 1 &&
                                std::abs(taller.bounds().height() - wide.bounds().height() - 100) <
                                    3,
                            "Bottom text handle stretched the text or changed width.");
                    require(app.document.undo(), "Cannot undo bottom text resize.");
                    app.document.selected = 0;
                    dragTextEdge(4, {-80, -60});
                    auto top = app.document.items[0];
                    require(std::abs(top.b.y - wide.b.y) < .01f && top.a.x == wide.a.x &&
                                top.b.x == wide.b.x && top.fontSize == wide.fontSize,
                            "Top text handle did not keep the bottom edge and width anchored.");
                    require(app.document.undo(), "Cannot undo top text resize.");
                    app.document.selected = 0;
                    dragTextEdge(5, {-400, 0}, true);
                    require(app.document.items[0] == wide,
                            "Canceling text reflow did not restore the original frame.");
                    dragTextEdge(5, {-420, 0});
                    auto narrow = app.document.items[0];
                    require(narrow.fontSize == wide.fontSize && lineCount(narrow) > 1 &&
                                narrow.bounds().height() > wide.bounds().height(),
                            "Narrowing text did not wrap and grow vertically.");
                    dragTextEdge(6, {0, -1000});
                    require(app.document.items[0].bounds().height() >=
                                narrow.bounds().height() - .01f,
                            "Shrinking a text frame clipped away lines.");
                    beginTextEditing({}, 0);
                    require(app.textEdit, "Cannot edit independently resized text.");
                    SendMessageW(app.textEdit, WM_CHAR, L'!', 0);
                    verify(L"resized-text-" + std::to_wstring(layout) + L"-" +
                           std::to_wstring(static_cast<int>(dpi * 100)) +
                           (boxed ? L"-box" : L"-plain"));
                    finishTextEditing();
                    require(app.document.items[0].textFrame &&
                                app.document.items[0].text.back() == L'!',
                            "Editing discarded the explicit text frame.");
                    require(app.image.pixels == pixels && app.colors == toolColors &&
                                app.fontSize == fontPreference,
                            "Text frame resizing changed screenshot pixels or drawing defaults.");
                }
        // Explicit Enter/newline characters stay explicit even when the frame is widened.
        app.dpi = 1;
        app.document.items[0].text = L"First line\r\nSecond line";
        app.graphics.measureText(app.document.items[0]);
        app.document.selected = 0;
        dragTextEdge(5, {400, 0});
        require(lineCount(app.document.items[0]) == 2 &&
                    app.document.items[0].text == L"First line\r\nSecond line",
                "Text reflow removed explicit line breaks.");
        std::cout << "PASS\n";
    }
    catch (const std::exception &exception)
    {
        std::cout << "FAIL: " << exception.what() << "\n";
        result = 1;
    }
    if (app.window)
        DestroyWindow(app.window);
    resetPreview();
    app.workspaceBrush.reset();
    app.target.reset();
    if (com)
        CoUninitialize();
    SetThreadDesktop(originalDesktop);
    if (desktop)
        CloseDesktop(desktop);
    SetProcessWindowStation(originalStation);
    if (station)
        CloseWindowStation(station);
    return result;
}
