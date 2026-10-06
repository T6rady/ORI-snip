#pragma once
#include "model.h"
#include <filesystem>
#include <utility>

namespace snip
{
struct Setting
{
    std::wstring section, key, value;
};
struct SettingsSection
{
    std::wstring name;
    std::vector<std::pair<std::wstring, std::wstring>> values;
};
uint32_t preferenceUInt(const std::wstring &path, const wchar_t *section, const wchar_t *key,
                        uint32_t fallback);
std::vector<Color> loadPalette(const std::wstring &path, const std::vector<Color> &defaults);
void commitPreferences(const std::wstring &path, const std::vector<Setting> &changes,
                       const std::vector<SettingsSection> &sections = {});
std::wstring settingsPathForProfile(const std::filesystem::path &localData,
                                    const std::filesystem::path &portable);
std::wstring personalSettingsPath();
} // namespace snip
