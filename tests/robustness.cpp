// Failure paths use isolated files/registry keys and test-only hooks absent from the release.
#include "../src/main.cpp"
#include <iostream>

namespace
{
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
template <class Action> void mustFail(Action action, const char *message)
{
    bool failed = false;
    try
    {
        action();
    }
    catch (...)
    {
        failed = true;
    }
    require(failed, message);
}
unsigned faultAt = 0;
void graphicsFault(unsigned checkpoint)
{
    if (checkpoint == faultAt)
        throw std::bad_alloc();
}
void settingsFault(unsigned checkpoint)
{
    if (checkpoint == faultAt)
        throw std::runtime_error("Injected staged write failure.");
}
unsigned errors = 0;
bool unknownFault = false;
void callbackFault(const char *, unsigned)
{
    if (unknownFault)
        throw 7;
    throw std::bad_alloc();
}
void countError(const char *)
{
    ++errors;
}
std::vector<char> contents(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
} // namespace
int wmain()
{
    const auto root = createTestDirectory(std::filesystem::current_path() / L"robustness-output");
    const auto path = root / L"preferences.ini";
    const auto registryPath = L"Software\\Tiger Snip Tests\\" + root.filename().wstring();
    HKEY key = nullptr;
    int result = 0;
    try
    {
        require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
                "COM initialization failed.");
        app.iniPath = path.wstring();
        const std::vector<Color> defaults{rgb(1, 2, 3)};
        SettingsSection palette{L"Palette", {{L"Count", L"64"}}};
        for (size_t i = 0; i < MaxPaletteColors; ++i)
            palette.values.emplace_back(std::to_wstring(i), std::to_wstring(i));
        commitPreferences(app.iniPath, {}, {palette});
        require(loadPalette(app.iniPath, defaults).size() == MaxPaletteColors,
                "Maximum palette rejected.");
        for (const auto *bad :
             {L"65", L"-1", L"banana", L"4294967296", L"99999999999999999999999999"})
        {
            commitPreferences(app.iniPath, {{L"Palette", L"Count", bad}});
            require(loadPalette(app.iniPath, defaults) == defaults,
                    "Malformed/oversized count accepted.");
        }
        commitPreferences(app.iniPath, {},
                          {{L"Palette",
                            {{L"Count", L"4"},
                             {L"0", L"255"},
                             {L"1", L"255"},
                             {L"2", L"-1"},
                             {L"3", L"16777216"}}}});
        require(loadPalette(app.iniPath, defaults) == std::vector<Color>{255},
                "Invalid/duplicate color accepted.");
        require(ColorFirst + static_cast<int>(MaxPaletteColors) <= RecentChoiceFirst,
                "Palette/Recent IDs overlap.");
        const auto colors = app.colors;
        command(RecentChoiceFirst);
        require(app.colors == colors, "Recent command dispatched as a palette color.");

        require(RegCreateKeyExW(HKEY_CURRENT_USER, registryPath.c_str(), 0, nullptr, 0,
                                KEY_ALL_ACCESS, nullptr, &key, nullptr) == ERROR_SUCCESS,
                "Cannot create isolated registry fixture.");
        require(!readRegistryString(HKEY_CURRENT_USER, registryPath.c_str(), L"missing"),
                "Missing registry value accepted.");
        auto registry = [&](DWORD type, const void *data, DWORD bytes) {
            require(RegSetValueExW(key, L"test", 0, type, static_cast<const BYTE *>(data), bytes) ==
                        ERROR_SUCCESS,
                    "Cannot write registry fixture.");
            return readRegistryString(HKEY_CURRENT_USER, registryPath.c_str(), L"test");
        };
        const wchar_t good[] = L"\"C:\\Tiger Snip.exe\" --tray";
        require(registry(REG_SZ, good, sizeof(good)) == good, "Valid startup string rejected.");
        require(!registry(REG_BINARY, good, sizeof(good)), "Wrong registry type accepted.");
        // Windows may normalize strings when storing them; exercise the raw parser too.
        registry(REG_SZ, good, sizeof(good) - sizeof(wchar_t));
        require(!boundedRegistryString(REG_SZ, good, sizeof(good) - sizeof(wchar_t)),
                "Unterminated registry value accepted.");
        registry(REG_SZ, good, sizeof(good) - 1);
        require(!boundedRegistryString(REG_SZ, good, sizeof(good) - 1),
                "Odd registry byte count accepted.");
        const wchar_t embedded[] = {L'A', 0, L'B', 0};
        require(!registry(REG_SZ, embedded, sizeof(embedded)), "Embedded terminator accepted.");
        std::vector<wchar_t> oversized(32769, L'X');
        oversized.back() = 0;
        require(!registry(REG_SZ, oversized.data(),
                          static_cast<DWORD>(oversized.size() * sizeof(wchar_t))),
                "Oversized registry value accepted.");

        testing::graphicsCheckpoint = graphicsFault;
        for (faultAt = 1; faultAt <= 3; ++faultAt)
        {
            Graphics graphics;
            mustFail([&] { graphics.initialize(); }, "Graphics failure injection did not fire.");
            require(!graphics.factory && !graphics.textFactory && !graphics.font &&
                        !graphics.roundStroke,
                    "Partial graphics state was published.");
            testing::graphicsCheckpoint = nullptr;
            graphics.initialize();
            require(graphics.factory && graphics.font && graphics.labelFont && graphics.dotStroke,
                    "Graphics retry did not finish.");
            testing::graphicsCheckpoint = graphicsFault;
        }
        testing::graphicsCheckpoint = nullptr;
        testing::errorSink = countError;
        testing::callbackCheckpoint = callbackFault;
        for (bool isUnknown : {false, true})
        {
            unknownFault = isUnknown;
            require(settingsProcedure(nullptr, WM_CREATE, 0, 0) == -1,
                    "Settings creation failure did not abort creation.");
            require(mainProcedure(nullptr, WM_CREATE, 0, 0) == -1,
                    "Main creation failure did not abort creation.");
            require(shortcutFieldProcedure(nullptr, WM_NULL, 0, 0, 0, 0) == 0,
                    "Shortcut callback failure escaped.");
            require(textEditProcedure(nullptr, WM_NULL, 0, 0, 0, 0) == 0,
                    "Text callback failure escaped.");
            require(overlayProcedure(nullptr, WM_NULL, 0, 0) == 0,
                    "Overlay callback failure escaped.");
            require(snip::colorPickerProcedure(nullptr, WM_INITDIALOG, 0, 0) == FALSE,
                    "Color dialog failure escaped.");
            require(snip::spectrumProcedure(nullptr, WM_NULL, 0, 0, 0, 0) == 0,
                    "Spectrum failure escaped.");
        }
        require(errors == 14, "A callback did not report its failure.");
        require(callbackBoundary<int>([]() -> int { throw 1; }, [](const char *) { throw 2; },
                                      -1) == -1,
                "Recovery exception escaped.");
        testing::callbackCheckpoint = nullptr;
        testing::errorSink = nullptr;
        require(windowsError("Operation", E_ACCESSDENIED).find("0x80070005") != std::string::npos,
                "HRESULT code missing.");
        require(!executablePath().empty(), "Executable path missing.");
        require(messageAvailable(1, 0) && !messageAvailable(0, 0),
                "Message result handling changed.");
        mustFail([] { messageAvailable(-1, ERROR_INVALID_WINDOW_HANDLE); },
                 "GetMessage error treated as a normal quit.");

        commitPreferences(app.iniPath, {{L"Unknown", L"Keep", L"27"},
                                        {L"Settings", L"Hotkey", L"12"},
                                        {L"Settings", L"InstantHotkey", L"13"}});
        const auto original = contents(path);
        app.hotkey = 22;
        app.instantHotkey = 23;
        app.shortcutsDirty = app.rendererPreferencesDirty = app.exportPreferencesDirty =
            app.toolPreferencesDirty = true;
        app.softwareRendering = true;
        faultAt = 3;
        testing::settingsCheckpoint = settingsFault;
        require(!saveToolPreferences(), "Partial settings failure accepted.");
        require(contents(path) == original, "Partial settings update reached destination.");
        require(app.shortcutsDirty && app.rendererPreferencesDirty && app.exportPreferencesDirty &&
                    app.toolPreferencesDirty,
                "Pending settings lost on failure.");
        testing::settingsCheckpoint = nullptr;
        require(saveToolPreferences(), "Pending settings retry failed.");
        require(preferenceUInt(app.iniPath, L"Settings", L"Hotkey", 0) == 22 &&
                    preferenceUInt(app.iniPath, L"Settings", L"InstantHotkey", 0) == 23,
                "Shortcut pair did not save together.");
        require(preferenceUInt(app.iniPath, L"Unknown", L"Keep", 0) == 27,
                "Unrelated settings lost.");
        require(preferenceUInt(app.iniPath, L"Settings", L"SoftwareRendering", 0) == 1,
                "Renderer retry lost.");
        require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY),
                "Cannot make read-only fixture.");
        app.toolPreferencesDirty = true;
        const auto readOnly = contents(path);
        require(!saveToolPreferences() && app.toolPreferencesDirty && contents(path) == readOnly,
                "Read-only preferences were overwritten or pending state lost.");
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        require(saveToolPreferences(), "Read-only retry failed.");
        saveBytes(path.wstring(), {0xff, 0xfe, 0x41});
        const auto damaged = contents(path);
        mustFail([&] { commitPreferences(app.iniPath, {{L"Settings", L"Hotkey", L"99"}}); },
                 "Damaged Unicode preferences accepted.");
        require(contents(path) == damaged, "Damaged settings were destroyed.");
        mustFail(
            [&] {
                commitPreferences((root / L"missing" / L"settings.ini").wstring(),
                                  {{L"Settings", L"Hotkey", L"99"}});
            },
            "Unwritable preferences directory accepted.");
        checkedTestReport(root / L"report.txt", "PASS: current report\n");
        mustFail([&] { checkedTestReport(root / L"missing" / L"report.txt", "PASS\n"); },
                 "Report open failure ignored.");
        failedTestReport(root / L"missing" / L"report.txt", "Expected failure");
        require(createTestDirectory(root) != createTestDirectory(root),
                "Test invocations share output paths.");
        std::cout << "PASS: palette bounds, bounded registry strings, graphics late-failure/retry, "
                     "callback standard/unknown/recovery failures, API errors, atomic settings and "
                     "retry, damaged/read-only settings, checked isolated reports.\n";
    }
    catch (const std::exception &failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        result = 1;
    }
    testing::settingsCheckpoint = nullptr;
    testing::graphicsCheckpoint = nullptr;
    testing::callbackCheckpoint = nullptr;
    testing::errorSink = nullptr;
    if (key)
        RegCloseKey(key);
    RegDeleteTreeW(HKEY_CURRENT_USER, registryPath.c_str());
    CoUninitialize();
    return result;
}
