#include "settings.h"
#include "graphics.h"
#include "commands.h"
#include "test_hooks.h"
#include <shlobj.h>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cwctype>
#include <limits>

namespace snip
{
namespace
{
std::vector<uint8_t> readSettingsBytes(const std::filesystem::path &path)
{
    std::error_code failure;
    const auto size = std::filesystem::file_size(path, failure);
    if (failure)
        throwWindowsError("Cannot read the preferences file.", failure.value());
    if (size > 1024 * 1024)
        throw std::runtime_error("The preferences file is too large (maximum 1 MB).");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open the preferences file.");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read the complete preferences file.");
    return bytes;
}
std::vector<uint8_t> unicodeIni(const std::vector<uint8_t> &source)
{
    std::wstring text;
    if (source.size() >= 2 && source[0] == 0xff && source[1] == 0xfe)
    {
        if (source.size() % 2)
            throw std::runtime_error("The Unicode preferences file is damaged.");
        text.resize((source.size() - 2) / sizeof(wchar_t));
        std::memcpy(text.data(), source.data() + 2, source.size() - 2);
    }
    else if (!source.empty())
    {
        const bool utf8 =
            source.size() >= 3 && source[0] == 0xef && source[1] == 0xbb && source[2] == 0xbf;
        const size_t offset = utf8 ? 3 : 0;
        const auto *bytes = reinterpret_cast<const char *>(source.data() + offset);
        const int length = static_cast<int>(source.size() - offset);
        if (length)
        {
            const UINT encoding = utf8 ? CP_UTF8 : CP_ACP;
            const DWORD flags = utf8 ? MB_ERR_INVALID_CHARS : 0;
            const int needed = MultiByteToWideChar(encoding, flags, bytes, length, nullptr, 0);
            if (!needed)
                throwWindowsError("Cannot decode the preferences file.");
            text.resize(needed);
            if (!MultiByteToWideChar(encoding, flags, bytes, length, text.data(), needed))
                throwWindowsError("Cannot decode the preferences file.");
        }
    }
    if (text.find(L'\0') != std::wstring::npos)
        throw std::runtime_error("The preferences file contains damaged text.");
    std::vector<uint8_t> result(2 + text.size() * sizeof(wchar_t));
    result[0] = 0xff;
    result[1] = 0xfe;
    std::memcpy(result.data() + 2, text.data(), text.size() * sizeof(wchar_t));
    return result;
}
struct StagedSettings
{
    std::filesystem::path path;
    ~StagedSettings()
    {
        if (!path.empty())
        {
            WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
            DeleteFileW(path.c_str());
        }
    }
};
} // namespace
uint32_t preferenceUInt(const std::wstring &path, const wchar_t *section, const wchar_t *key,
                        uint32_t fallback)
{
    wchar_t value[64]{};
    const DWORD length = GetPrivateProfileStringW(
        section, key, L"", value, static_cast<DWORD>(std::size(value)), path.c_str());
    if (!length || length == std::size(value) - 1)
        return fallback;
    uint64_t result = 0;
    for (DWORD i = 0; i < length; ++i)
    {
        if (value[i] < L'0' || value[i] > L'9')
            return fallback;
        result = result * 10 + (value[i] - L'0');
        if (result > std::numeric_limits<uint32_t>::max())
            return fallback;
    }
    return static_cast<uint32_t>(result);
}
std::vector<Color> loadPalette(const std::wstring &path, const std::vector<Color> &defaults)
{
    const auto count = preferenceUInt(path, L"Palette", L"Count", UINT32_MAX);
    if (count > MaxPaletteColors)
        return defaults;
    std::vector<Color> result;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto value = preferenceUInt(path, L"Palette", std::to_wstring(i).c_str(), UINT32_MAX);
        if (value <= 0xffffff && std::find(result.begin(), result.end(), value) == result.end())
            result.push_back(value);
    }
    return result;
}
void commitPreferences(const std::wstring &path, const std::vector<Setting> &changes,
                       const std::vector<SettingsSection> &sections)
{
    if (changes.empty() && sections.empty())
        return;
    const auto destination = std::filesystem::absolute(path);
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    const bool exists = attributes != INVALID_FILE_ATTRIBUTES;
    if (!exists && GetLastError() != ERROR_FILE_NOT_FOUND)
        throwWindowsError("Cannot access the preferences file.");
    if (exists && (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)))
        throwWindowsError("The preferences file is read-only.", ERROR_ACCESS_DENIED);
    const auto original = exists ? readSettingsBytes(destination) : std::vector<uint8_t>{};
    GUID unique{};
    check(CoCreateGuid(&unique), "Cannot create a preferences transaction.");
    wchar_t id[40]{};
    StringFromGUID2(unique, id, static_cast<int>(std::size(id)));
    StagedSettings staged{destination.parent_path() /
                          (std::wstring(L".tiger-snip-settings-") + id + L".ini")};
    saveBytes(staged.path.wstring(), unicodeIni(original));
#ifdef TIGER_SNIP_TESTING
    unsigned checkpoint = 0;
#endif
    for (const auto &change : changes)
    {
        if (!WritePrivateProfileStringW(change.section.c_str(), change.key.c_str(),
                                        change.value.c_str(), staged.path.c_str()))
            throwWindowsError("Cannot stage the preference change.");
#ifdef TIGER_SNIP_TESTING
        if (testing::settingsCheckpoint)
            testing::settingsCheckpoint(++checkpoint);
#endif
    }
    for (const auto &section : sections)
    {
        std::wstring entries;
        for (const auto &[key, value] : section.values)
        {
            entries += key + L"=" + value;
            entries.push_back(L'\0');
        }
        entries.push_back(L'\0');
        if (!WritePrivateProfileSectionW(section.name.c_str(), entries.c_str(),
                                         staged.path.c_str()))
            throwWindowsError("Cannot stage the preference section.");
#ifdef TIGER_SNIP_TESTING
        if (testing::settingsCheckpoint)
            testing::settingsCheckpoint(++checkpoint);
#endif
    }
    // The profile API's cache-flush operation intentionally returns zero.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, staged.path.c_str());
    saveBytes(destination.wstring(), readSettingsBytes(staged.path));
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, destination.c_str());
}
std::wstring settingsPathForProfile(const std::filesystem::path &localData,
                                    const std::filesystem::path &portable)
{
    const auto directory = localData / L"Tiger Snip";
    std::error_code failure;
    std::filesystem::create_directories(directory, failure);
    if (failure)
        throw std::runtime_error("Cannot create your personal Tiger Snip settings folder.");
    const auto destination = directory / L"TigerSnip.ini";
    if (!std::filesystem::exists(destination))
    {
        // Import a portable user's own preferences once. Setup never distributes an INI.
        if (std::filesystem::is_regular_file(portable))
        {
            if (CopyFileW(portable.c_str(), destination.c_str(), TRUE))
            {
                // A read-only release folder must not make personal preferences read-only.
                const DWORD attributes = GetFileAttributesW(destination.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES ||
                    ((attributes & FILE_ATTRIBUTE_READONLY) &&
                     !SetFileAttributesW(destination.c_str(),
                                         attributes & ~FILE_ATTRIBUTE_READONLY)))
                    throw std::runtime_error("Cannot make migrated personal preferences writable.");
            }
            else if (GetLastError() != ERROR_FILE_EXISTS)
                throw std::runtime_error("Cannot migrate your existing Tiger Snip preferences.");
        }
    }
    return destination.wstring();
}
std::wstring personalSettingsPath()
{
    PWSTR folder = nullptr;
    check(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder),
          "Cannot locate your personal application settings folder.");
    const std::filesystem::path localData(folder);
    CoTaskMemFree(folder);
    return settingsPathForProfile(localData, std::filesystem::path(executablePath()).parent_path() /
                                                 L"TigerSnip.ini");
}

} // namespace snip
