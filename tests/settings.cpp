// Verify per-profile storage and one-time migration using synthetic profiles.
#include "../src/main.cpp"
#include <iostream>

int wmain()
{
    const auto root = std::filesystem::current_path() /
        (L"settings-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto portable = root / L"portable.ini";
    const auto missing = root / L"missing.ini";
    const auto first = root / L"first" / L"Tiger Snip" / L"TigerSnip.ini";
    const auto second = root / L"second" / L"Tiger Snip" / L"TigerSnip.ini";
    const auto third = root / L"third" / L"Tiger Snip" / L"TigerSnip.ini";
    auto require = [](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(message);
    };
    int result = 0;
    try
    {
        std::filesystem::create_directory(root);
        require(WritePrivateProfileStringW(L"Settings", L"SoftwareRendering", L"1", portable.c_str()),
                "Cannot create portable preferences.");
        require(WritePrivateProfileStringW(L"Settings", L"AutoCopy", L"0", portable.c_str()),
                "Cannot create portable auto-copy preference.");
        require(WritePrivateProfileStringW(L"ToolPreferences", L"StrokeWidth", L"13", portable.c_str()),
                "Cannot create portable stroke preference.");
        require(SetFileAttributesW(portable.c_str(), FILE_ATTRIBUTE_READONLY),
                "Cannot make the migration source read-only.");
        require(settingsPathForProfile(root / L"first", portable) == first.wstring(),
                "Preferences must be under the requested user's profile.");
        app.iniPath = first.wstring();
        loadToolPreferences();
        require(app.softwareRendering && !app.autoCopy && app.thickness == 13,
                "Migration must preserve this PC's renderer and personal tool preferences.");
        app.thickness = 17;
        app.toolPreferencesDirty = true;
        require(saveToolPreferences(), "Migrated personal preferences must be writable.");
        settingsPathForProfile(root / L"first", portable);
        require(GetPrivateProfileIntW(L"ToolPreferences", L"StrokeWidth", 0, first.c_str()) == 17,
                "A later launch must not overwrite existing personal preferences.");

        require(settingsPathForProfile(root / L"second", missing) == second.wstring(),
                "Another user must have a separate destination.");
        app.iniPath = second.wstring();
        loadToolPreferences();
        require(!app.softwareRendering && app.autoCopy && app.thickness == 4,
                "A fresh user must retain the ordinary renderer and tool defaults.");
        app.thickness = 23;
        app.toolPreferencesDirty = true;
        require(saveToolPreferences(), "The second profile must be writable.");
        require(GetPrivateProfileIntW(L"ToolPreferences", L"StrokeWidth", 0, second.c_str()) == 23 &&
                GetPrivateProfileIntW(L"ToolPreferences", L"StrokeWidth", 0, first.c_str()) == 17 &&
                GetPrivateProfileIntW(L"ToolPreferences", L"StrokeWidth", 0, portable.c_str()) == 13,
                "Saving one user's preferences must not change another user or the release folder.");
        require(settingsPathForProfile(root / L"third", missing) == third.wstring() &&
                !std::filesystem::exists(third), "Locating settings must not distribute someone else's INI.");
        std::cout << "PASS: separate profile settings, read-only portable migration, no repeated overwrite, "
                     "ordinary defaults for new users, independent writes, no release-folder writes.\n";
    }
    catch (const std::exception &failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        result = 1;
    }
    // Only the known synthetic files and empty directories belong to this test.
    SetFileAttributesW(portable.c_str(), FILE_ATTRIBUTE_NORMAL);
    std::error_code ignored;
    std::filesystem::remove(portable, ignored);
    for (const auto &file : {first, second, third})
    {
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        std::filesystem::remove(file, ignored);
        std::filesystem::remove(file.parent_path(), ignored);
        std::filesystem::remove(file.parent_path().parent_path(), ignored);
    }
    std::filesystem::remove(root, ignored);
    return result;
}
