#include "test_reports.h"
#include "windows_support.h"
#include <objbase.h>
#include <fstream>
#include <stdexcept>

namespace snip
{
std::filesystem::path createTestDirectory(const std::filesystem::path &root)
{
    GUID unique{};
    check(CoCreateGuid(&unique), "Cannot identify this test run.");
    wchar_t id[40]{};
    StringFromGUID2(unique, id, static_cast<int>(std::size(id)));
    const auto directory = std::filesystem::absolute(root) / id;
    std::filesystem::create_directories(directory);
    return directory;
}
void checkedTestReport(const std::filesystem::path &path, const std::string &content)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("Cannot open the test report for writing.");
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.flush();
    if (!file)
        throw std::runtime_error("Cannot write the complete test report.");
    file.close();
    if (!file)
        throw std::runtime_error("Cannot close the test report.");
}
void failedTestReport(const std::filesystem::path &path, const char *failure) noexcept
{
    if (path.parent_path().empty())
    {
        OutputDebugStringA(failure);
        return;
    }
    try
    {
        checkedTestReport(path, std::string("FAIL: ") + failure + "\n");
    }
    catch (...)
    {
        OutputDebugStringW(L"Tiger Snip: unable to write the failed test report.\n");
    }
}
} // namespace snip
